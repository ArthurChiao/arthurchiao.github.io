---
layout    : post
title     : "源码解析：K8s 创建 pod 时，背后发生了什么（五）（2021）"
date      : 2021-06-01
lastupdate: 2021-06-01
categories: k8s cni
---

本文基于 2019 年的一篇文章
[What happens when ... Kubernetes edition!](https://github.com/jamiehannaford/what-happens-when-k8s)
**<mark>梳理了 k8s 创建 pod</mark>**（及其 deployment/replicaset）**<mark>的整个过程</mark>**，
整理了每个**重要步骤的代码调用栈**，以**<mark>在实现层面加深对整个过程的理解</mark>**。

原文参考的 k8S 代码已经较老（`v1.8`/`v1.14` 以及当时的 `master`），且部分代码
链接已失效；**本文代码基于 [`v1.21`](https://github.com/kubernetes/kubernetes/tree/v1.21.1)**。

由于内容已经不与原文一一对应（有增加和删减），因此标题未加 “[译]” 等字样。感谢原作者（们）的精彩文章。

篇幅太长，分成了几部分：

1. [源码解析：K8s 创建 pod 时，背后发生了什么（一）（2021）]({% link _posts/2021-06-01-what-happens-when-k8s-creates-pods-1-zh.md %})
1. [源码解析：K8s 创建 pod 时，背后发生了什么（二）（2021）]({% link _posts/2021-06-01-what-happens-when-k8s-creates-pods-2-zh.md %})
1. [源码解析：K8s 创建 pod 时，背后发生了什么（三）（2021）]({% link _posts/2021-06-01-what-happens-when-k8s-creates-pods-3-zh.md %})
1. [源码解析：K8s 创建 pod 时，背后发生了什么（四）（2021）]({% link _posts/2021-06-01-what-happens-when-k8s-creates-pods-4-zh.md %})
1. [源码解析：K8s 创建 pod 时，背后发生了什么（五）（2021）]({% link _posts/2021-06-01-what-happens-when-k8s-creates-pods-5-zh.md %})

----

* TOC
{:toc}

----

# 6 kubelet

每个 K8s node 上都会运行一个名为 kubelet 的 agent，它负责

* pod 生命周期管理。

    这意味着，它负责将 “Pod” 的逻辑抽象（etcd 中的元数据）转换成具体的容器（container）。

* 挂载目录
* 创建容器日志
* 垃圾回收等等

## 6.1 Pod sync（状态同步）

**<mark>kubelet 也可以认为是一个 controller</mark>**，它

1. 通过 ListWatch 接口，从 kube-apiserver **<mark>获取属于本节点的 Pod 列表</mark>**（根据
  `spec.nodeName` [过滤](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/kubelet/config/apiserver.go#L32)），
2. 然后**<mark>与自己缓存的 pod 列表对比</mark>**，如果有 pod 创建、删除、更新等操作，就开始同步状态。

下面具体看一下同步过程。

### 同步过程

```go
// pkg/kubelet/kubelet.go

// syncPod is the transaction script for the sync of a single pod.
func (kl *Kubelet) syncPod(o syncPodOptions) error {
    pod := o.pod

    if updateType == SyncPodKill { // kill pod 操作
        kl.killPod(pod, nil, podStatus, PodTerminationGracePeriodSecondsOverride)
        return nil
    }

    firstSeenTime := pod.Annotations["kubernetes.io/config.seen"] // 测量 latency，从 apiserver 第一次看到 pod 算起

    if updateType == SyncPodCreate { // create pod 操作
        if !firstSeenTime.IsZero() { // Record pod worker start latency if being created
            metrics.PodWorkerStartDuration.Observe(metrics.SinceInSeconds(firstSeenTime))
        }
    }

    // Generate final API pod status with pod and status manager status
    apiPodStatus := kl.generateAPIPodStatus(pod, podStatus)

    podStatus.IPs = []string{}
    if len(podStatus.IPs) == 0 && len(apiPodStatus.PodIP) > 0 {
        podStatus.IPs = []string{apiPodStatus.PodIP}
    }

    runnable := kl.canRunPod(pod)
    if !runnable.Admit { // Pod is not runnable; update the Pod and Container statuses to why.
        apiPodStatus.Reason = runnable.Reason
        ...
    }

    kl.statusManager.SetPodStatus(pod, apiPodStatus)

    // Kill pod if it should not be running
    if !runnable.Admit || pod.DeletionTimestamp != nil || apiPodStatus.Phase == v1.PodFailed {
        return kl.killPod(pod, nil, podStatus, nil)
    }

    // 如果 network plugin not ready，并且 pod 网络不是 host network 类型，返回相应错误
    if err := kl.runtimeState.networkErrors(); err != nil && !IsHostNetworkPod(pod) {
        return fmt.Errorf("%s: %v", NetworkNotReadyErrorMsg, err)
    }

    // Create Cgroups for the pod and apply resource parameters if cgroups-per-qos flag is enabled.
    pcm := kl.containerManager.NewPodContainerManager()

    if kubetypes.IsStaticPod(pod) { // Create Mirror Pod for Static Pod if it doesn't already exist
        ...
    }

    kl.makePodDataDirs(pod)                     // Make data directories for the pod
    kl.volumeManager.WaitForAttachAndMount(pod) // Wait for volumes to attach/mount
    pullSecrets := kl.getPullSecretsForPod(pod) // Fetch the pull secrets for the pod

    // Call the container runtime's SyncPod callback
    result := kl.containerRuntime.SyncPod(pod, podStatus, pullSecrets, kl.backOff)
    kl.reasonCache.Update(pod.UID, result)
}
```

1. 如果是 pod 创建事件，会记录一些 pod latency 相关的 metrics；
1. 然后调用 `generateAPIPodStatus()` **<mark>生成一个 v1.PodStatus 对象</mark>**，代表 pod 当前阶段（Phase）的状态。

    Pod 的 Phase 是对其生命周期中不同阶段的高层抽象，非常复杂，后面会介绍。

1. PodStatus 生成之后，将发送给 Pod status manager，后者的任务是**<mark>异步地通过 apiserver 更新 etcd 记录</mark>**。
1. 接下来会**<mark>运行一系列 admission handlers</mark>**，确保 pod 有正确的安全权限（security permissions）。

    其中包括 enforcing [AppArmor profiles and `NO_NEW_PRIVS`](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/kubelet/kubelet.go#L883-L884)。
    在这个阶段**<mark>被 deny 的 Pods 将无限期处于 Pending 状态</mark>**。

1. 如果指定了 `cgroups-per-qos`，kubelet 将为这个 pod 创建 cgroups。可以实现更好的 QoS。
1. **<mark>为容器创建一些目录</mark>**。包括

    * pod 目录 （一般是 `/var/run/kubelet/pods/<podID>`）
    * volume 目录 (`<podDir>/volumes`)
    * plugin 目录 (`<podDir>/plugins`).

1. volume manager 将 [等待](https://github.com/kubernetes/kubernetes/blob/2723e06a251a4ec3ef241397217e73fa782b0b98/pkg/kubelet/volumemanager/volume_manager.go#L330)
    `Spec.Volumes` 中定义的 volumes attach 完成。取决于 volume 类型，pod 可能会等待很长时间（例如 cloud 或 NFS volumes）。

1. 从 apiserver 获取 `Spec.ImagePullSecrets` 中指定的 **<mark>secrets，注入容器</mark>**。

1. **<mark>容器运行时（runtime）创建容器</mark>**（后面详细描述）。

### Pod 状态

前面提到，`generateAPIPodStatus()` [生成一个 v1.PodStatus](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/kubelet/kubelet_pods.go#L1287)
对象，代表 pod 当前阶段（Phase）的状态。

Pod 的 Phase 是对其生命周期中不同阶段的高层抽象，包括

* `Pending`
* `Running`
* `Succeeded`
* `Failed`
* `Unknown`

生成这个状态的过程非常复杂，一些细节如下：

1. 首先，顺序执行一系列 `PodSyncHandlers` 。每个 handler **<mark>判断这个 pod 是否还应该留在这个 node 上</mark>**。
  如果其中任何一个判断结果是否，那 pod 的 phase [将变为](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/kubelet/kubelet_pods.go#L1293-L1297)
  `PodFailed` 并最终会被**<mark>从这个 node 驱逐</mark>**。

    一个例子是 pod 的 `activeDeadlineSeconds` （Jobs 中会用到）超时之后，就会被驱逐。

1. 接下来决定 Pod Phase 的将是其 init 和 real containers。由于此时容器还未启动，因此
  将**<mark>处于</mark>** [waiting](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/kubelet/kubelet_pods.go#L1244) **<mark>状态</mark>**。
  **有 waiting 状态 container 的 pod，将处于 [`Pending`](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/kubelet/kubelet_pods.go#L1258-L1261) Phase**。

1. 由于此时容器运行时还未创建我们的容器
  ，因此它将把 [`PodReady` 字段置为 False](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/kubelet/status/generate.go#L70-L81).

## 6.2 CRI 及创建 pause 容器

至此，大部分准备工作都已完成，接下来即将创建容器了。**创建容器是通过
Container Runtime （例如 `docker` 或 `rkt`）完成的**。

为实现可扩展，kubelet 从 v1.5.0 开始，**<mark>使用 CRI（Container Runtime Interface）与具体的容器运行时交互</mark>**。
简单来说，CRI 提供了 kubelet 和具体 runtime implementation 之间的抽象接口，
用 [protocol buffers](https://github.com/google/protobuf) 和 gRPC 通信。

### CRI SyncPod

```go
// pkg/kubelet/kuberuntime/kuberuntime_manager.go

// SyncPod syncs the running pod into the desired pod by executing following steps:
//  1. Compute sandbox and container changes.
//  2. Kill pod sandbox if necessary.
//  3. Kill any containers that should not be running.
//  4. Create sandbox if necessary.
//  5. Create ephemeral containers.
//  6. Create init containers.
//  7. Create normal containers.
//
func (m *kubeGenericRuntimeManager) SyncPod(pod *v1.Pod, podStatus *kubecontainer.PodStatus,
    pullSecrets []v1.Secret, backOff *flowcontrol.Backoff) (result kubecontainer.PodSyncResult) {

    // Step 1: Compute sandbox and container changes.
    podContainerChanges := m.computePodActions(pod, podStatus)
    if podContainerChanges.CreateSandbox {
        ref := ref.GetReference(legacyscheme.Scheme, pod)
        if podContainerChanges.SandboxID != "" {
            m.recorder.Eventf("Pod sandbox changed, it will be killed and re-created.")
        } else {
            InfoS("SyncPod received new pod, will create a sandbox for it")
        }
    }

    // Step 2: Kill the pod if the sandbox has changed.
    if podContainerChanges.KillPod {
        if podContainerChanges.CreateSandbox {
            InfoS("Stopping PodSandbox for pod, will start new one")
        } else {
            InfoS("Stopping PodSandbox for pod, because all other containers are dead")
        }

        killResult := m.killPodWithSyncResult(pod, ConvertPodStatusToRunningPod(m.runtimeName, podStatus), nil)
        result.AddPodSyncResult(killResult)

        if podContainerChanges.CreateSandbox {
            m.purgeInitContainers(pod, podStatus)
        }
    } else {
        // Step 3: kill any running containers in this pod which are not to keep.
        for containerID, containerInfo := range podContainerChanges.ContainersToKill {
            killContainerResult := NewSyncResult(kubecontainer.KillContainer, containerInfo.name)
            result.AddSyncResult(killContainerResult)
            m.killContainer(pod, containerID, containerInfo)
        }
    }

    // Keep terminated init containers fairly aggressively controlled
    // This is an optimization because container removals are typically handled by container GC.
    m.pruneInitContainersBeforeStart(pod, podStatus)

    // Step 4: Create a sandbox for the pod if necessary.
    podSandboxID := podContainerChanges.SandboxID
    if podContainerChanges.CreateSandbox {
        createSandboxResult := kubecontainer.NewSyncResult(kubecontainer.CreatePodSandbox, format.Pod(pod))
        result.AddSyncResult(createSandboxResult)
        podSandboxID, msg = m.createPodSandbox(pod, podContainerChanges.Attempt)
        podSandboxStatus := m.runtimeService.PodSandboxStatus(podSandboxID)
    }

    // the start containers routines depend on pod ip(as in primary pod ip)
    // instead of trying to figure out if we have 0 < len(podIPs) everytime, we short circuit it here
    podIP := ""
    if len(podIPs) != 0 {
        podIP = podIPs[0]
    }

    // Get podSandboxConfig for containers to start.
    configPodSandboxResult := kubecontainer.NewSyncResult(ConfigPodSandbox, podSandboxID)
    result.AddSyncResult(configPodSandboxResult)
    podSandboxConfig := m.generatePodSandboxConfig(pod, podContainerChanges.Attempt)

    // Helper containing boilerplate common to starting all types of containers.
    // typeName is a label used to describe this type of container in log messages,
    // currently: "container", "init container" or "ephemeral container"
    start := func(typeName string, spec *startSpec) error {
        startContainerResult := kubecontainer.NewSyncResult(kubecontainer.StartContainer, spec.container.Name)
        result.AddSyncResult(startContainerResult)

        isInBackOff, msg := m.doBackOff(pod, spec.container, podStatus, backOff)
        if isInBackOff {
            startContainerResult.Fail(err, msg)
            return err
        }

        m.startContainer(podSandboxID, podSandboxConfig, spec, pod, podStatus, pullSecrets, podIP, podIPs)
        return nil
    }

    // Step 5: start ephemeral containers
    // These are started "prior" to init containers to allow running ephemeral containers even when there
    // are errors starting an init container. In practice init containers will start first since ephemeral
    // containers cannot be specified on pod creation.
    for _, idx := range podContainerChanges.EphemeralContainersToStart {
        start("ephemeral container", ephemeralContainerStartSpec(&pod.Spec.EphemeralContainers[idx]))
    }

    // Step 6: start the init container.
    if container := podContainerChanges.NextInitContainerToStart; container != nil {
        start("init container", containerStartSpec(container))
    }

    // Step 7: start containers in podContainerChanges.ContainersToStart.
    for _, idx := range podContainerChanges.ContainersToStart {
        start("container", containerStartSpec(&pod.Spec.Containers[idx]))
    }
}
```

### CRI create sandbox

kubelet [发起 `RunPodSandbox`](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/kubelet/kuberuntime/kuberuntime_sandbox.go#L51) RPC 调用。

**<mark>“sandbox” 是一个 CRI 术语，它表示一组容器，在 K8s 里就是一个 Pod</mark>**。
这个词是有意用作比较宽泛的描述，这样对其他运行时的描述也是适用的（例如，在基于 hypervisor 的运行时中，sandbox 可能是一个虚拟机）。

```go
// pkg/kubelet/kuberuntime/kuberuntime_sandbox.go

// createPodSandbox creates a pod sandbox and returns (podSandBoxID, message, error).
func (m *kubeGenericRuntimeManager) createPodSandbox(pod *v1.Pod, attempt uint32) (string, string, error) {
    podSandboxConfig := m.generatePodSandboxConfig(pod, attempt)

    // 创建 pod log 目录
    m.osInterface.MkdirAll(podSandboxConfig.LogDirectory, 0755)

    runtimeHandler := ""
    if m.runtimeClassManager != nil {
        runtimeHandler = m.runtimeClassManager.LookupRuntimeHandler(pod.Spec.RuntimeClassName)
        if runtimeHandler != "" {
            InfoS("Running pod with runtime handler", runtimeHandler)
        }
    }

    podSandBoxID := m.runtimeService.RunPodSandbox(podSandboxConfig, runtimeHandler)
    return podSandBoxID, "", nil
}
```

```go
// pkg/kubelet/cri/remote/remote_runtime.go

// RunPodSandbox creates and starts a pod-level sandbox.
func (r *remoteRuntimeService) RunPodSandbox(config *PodSandboxConfig, runtimeHandler string) (string, error) {

    InfoS("[RemoteRuntimeService] RunPodSandbox", "config", config, "runtimeHandler", runtimeHandler)

    resp := r.runtimeClient.RunPodSandbox(ctx, &runtimeapi.RunPodSandboxRequest{
        Config:         config,
        RuntimeHandler: runtimeHandler,
    })

    InfoS("[RemoteRuntimeService] RunPodSandbox Response", "podSandboxID", resp.PodSandboxId)
    return resp.PodSandboxId, nil
}
```

### Create sandbox：docker 相关代码

前面是 CRI 通用代码，如果我们的容器 runtime 是 docker，那接下来就会调用到 docker 相关代码。

在这种 runtime 中，**<mark>创建一个 sandbox 会转换成创建一个 “pause” 容器的操作</mark>**。
Pause container 作为一个 pod 内其他所有容器的父角色，hold 了很多 pod-level 的资源，
具体说就是 Linux namespace，例如 IPC NS、Net NS、IPD NS。

"pause" container 提供了一种持有这些 ns、让所有子容器共享它们 的方式。
例如，共享 netns 的好处之一是，pod 内不同容器之间可以通过 localhost 方式访问彼此。
pause 容器的第二个用处是**<mark>回收（reaping）dead processes</mark>**。
更多信息，可参考 [这篇博客](https://www.ianlewis.org/en/almighty-pause-container)。

Pause 容器创建之后，会被 checkpoint 到磁盘，然后启动。

```go
// pkg/kubelet/dockershim/docker_sandbox.go

// 对于 docker runtime，PodSandbox 实现为一个 holding 网络命名空间（netns）的容器
func (ds *dockerService) RunPodSandbox(ctx context.Context, r *RunPodSandboxRequest) (*RunPodSandboxResponse) {

    // Step 1: Pull the image for the sandbox.
    ensureSandboxImageExists(ds.client, image)

    // Step 2: Create the sandbox container.
    createConfig := ds.makeSandboxDockerConfig(config, image)
    createResp := ds.client.CreateContainer(*createConfig)
    resp := &runtimeapi.RunPodSandboxResponse{PodSandboxId: createResp.ID}

    ds.setNetworkReady(createResp.ID, false) // 容器 network 状态初始化为 false

    // Step 3: Create Sandbox Checkpoint.
    CreateCheckpoint(createResp.ID, constructPodSandboxCheckpoint(config))

    // Step 4: Start the sandbox container。 如果失败，kubelet 会 GC 掉 sandbox
    ds.client.StartContainer(createResp.ID)

    rewriteResolvFile()

    // 如果是 hostNetwork 类型，到这里就可以返回了，无需下面的 CNI 流程
    if GetNetwork() == NamespaceMode_NODE {
        return resp, nil
    }

    // Step 5: Setup networking for the sandbox with CNI
    // 包括分配 IP、设置 sandbox 内的路由、创建虚拟网卡等。
    cID := kubecontainer.BuildContainerID(runtimeName, createResp.ID)
    ds.network.SetUpPod(Namespace, Name, cID, Annotations, networkOptions)

    return resp, nil
}
```

最后调用的 `SetUpPod()` 为容器创建网络，它有会**<mark>调用到 plugin manager 的同名方法</mark>**：

```go
// pkg/kubelet/dockershim/network/plugins.go

func (pm *PluginManager) SetUpPod(podNamespace, podName, id ContainerID, annotations, options) error {
    const operation = "set_up_pod"
    fullPodName := kubecontainer.BuildPodFullName(podName, podNamespace)

    // 调用 CNI 插件为容器设置网络
    pm.plugin.SetUpPod(podNamespace, podName, id, annotations, options)
}
```

> Cgroup 也很重要，是 Linux 掌管资源分配的方式，docker 利用它实现资源隔离。
> 更多信息，参考 [What even is a Container?](https://jvns.ca/blog/2016/10/10/what-even-is-a-container/)

## 6.3 CNI 前半部分：CNI plugin manager 处理

现在我们的 pod 已经有了一个占坑用的 pause 容器，它占住了 pod 需要用到的所有 namespace。
接下来需要做的就是：**<mark>调用底层的具体网络方案</mark>**（bridge/flannel/calico/cilium 等等）
提供的 CNI 插件，**<mark>创建并打通容器的网络</mark>**。

CNI 是 Container Network Interface 的缩写，工作机制与
Container Runtime Interface 类似。简单来说，CNI 是一个抽象接口，不同的网络提供商只要实现了 CNI
中的几个方法，就能接入 K8s，为容器创建网络。kubelet 与CNI 插件之间通过 JSON
数据交互（配置文件放在 `/etc/cni/net.d`），通过 stdin 将配置数据传递给 CNI binary (located in `/opt/cni/bin`)。

CNI 插件有自己的配置，例如，内置的 bridge 插件可能配置如下：

```yaml
{
    "cniVersion": "0.3.1",
    "name": "bridge",
    "type": "bridge",
    "bridge": "cnio0",
    "isGateway": true,
    "ipMasq": true,
    "ipam": {
        "type": "host-local",
        "ranges": [
          [{"subnet": "${POD_CIDR}"}]
        ],
        "routes": [{"dst": "0.0.0.0/0"}]
    }
}
```

还会通过 `CNI_ARGS` 环境变量传递 pod metadata，例如 name 和 ns。

### 调用栈概览

下面的调用栈是 CNI 前半部分：**<mark>CNI plugin manager 调用到具体的 CNI 插件</mark>**（可执行文件），
执行 shell 命令为容器创建网络：

```
SetUpPod                                                  // pkg/kubelet/dockershim/network/cni/cni.go
 |-ns = plugin.host.GetNetNS(id)
 |-plugin.addToNetwork(name, id, ns)                      // -> pkg/kubelet/dockershim/network/cni/cni.go
    |-plugin.buildCNIRuntimeConf
    |-cniNet.AddNetworkList(netConf)                      // -> github.com/containernetworking/cni/libcni/api.go
       |-for net := range list.Plugins
       |   result = c.addNetwork
       |              |-pluginPath = FindInPath(c.Path)
       |              |-ValidateContainerID(ContainerID)
       |              |-ValidateNetworkName(name)
       |              |-ValidateInterfaceName(IfName)
       |              |-invoke.ExecPluginWithResult(pluginPath, c.args("ADD", rt))
       |                        |-shell("/opt/cni/bin/xx <args>")
       |
       |-c.cacheAdd(result, list.Bytes, list.Name, rt)
```

最后一层调用 `ExecPlugin()`：

```go
// vendor/github.com/containernetworking/cni/pkg/invoke/raw_exec.go

func (e *RawExec) ExecPlugin(ctx, pluginPath, stdinData []byte, environ []string) ([]byte, error) {
    c := exec.CommandContext(ctx, pluginPath)
    c.Env = environ
    c.Stdin = bytes.NewBuffer(stdinData)
    c.Stdout = stdout
    c.Stderr = stderr

    for i := 0; i <= 5; i++ { // Retry the command on "text file busy" errors
        err := c.Run()
        if err == nil { // Command succeeded
            break
        }

        if strings.Contains(err.Error(), "text file busy") {
            time.Sleep(time.Second)
            continue
        }

        // All other errors except than the busy text file
        return nil, e.pluginErr(err, stdout.Bytes(), stderr.Bytes())
    }

    return stdout.Bytes(), nil
}
```

可以看到，经过上面的几层调用，最终是通过 shell 命令执行了宿主机上的 CNI 插件，
例如 `/opt/cni/bin/cilium-cni`，并通过 stdin 传递了一些 JSON 参数。

## 6.4 CNI 后半部分：CNI plugin 实现

下面看 CNI 处理的后半部分：CNI 插件为容器创建网络，也就是可执行文件 `/opt/cni/bin/xxx` 的实现。

CNI 相关的代码维护在一个**<mark>单独的项目</mark>** [github.com/containernetworking/cni](https://github.com/containernetworking/cni)。
每个 CNI 插件只需要实现其中的几个方法，然后**编译成独立的可执行文件**，放在 `/etc/cni/bin` 下面即可。
下面是一些具体的插件，

```shell
$ ls /opt/cni/bin/
bridge  cilium-cni  cnitool  dhcp  host-local  ipvlan  loopback  macvlan  noop
```

### 调用栈概览

CNI 插件（可执行文件）执行时会调用到 `PluginMain()`，从这往后的调用栈
（**注意源文件都是 `github.com/containernetworking/cni` 项目中的路径**）：

```
PluginMain                                                     // pkg/skel/skel.go
 |-PluginMainWithError                                         // pkg/skel/skel.go
   |-pluginMain                                                // pkg/skel/skel.go
      |-switch cmd {
          case "ADD":
            checkVersionAndCall(cmdArgs, cmdAdd)               // pkg/skel/skel.go
              |-configVersion = Decode(cmdArgs.StdinData)
              |-Check(configVersion, pluginVersionInfo)
              |-toCall(cmdArgs) // toCall == cmdAdd
                 |-cmdAdd(cmdArgs)
                   |-specific CNI plugin implementations
     
          case "DEL":
            checkVersionAndCall(cmdArgs, cmdDel)
          case "VERSION":
            versionInfo.Encode(t.Stdout)
          default:
            return createTypedError("unknown CNI_COMMAND: %v", cmd)
        }
```

可见**<mark>对于 kubelet 传过来的 "ADD" 命令，最终会调用到 CNI 插件的 cmdAdd() 方法</mark>** —— 该方法默认是空的，需要由每种 CNI 插件自己实现。
同理，删除 pod 时对应的是 `"DEL"` 操作，调用到的 `cmdDel()` 方法也是要由具体 CNI 插件实现的。

### CNI 插件实现举例：Bridge

[github.com/containernetworking/plugins](https://github.com/containernetworking/plugins)
项目中包含了很多种 CNI plugin 的实现，例如 IPVLAN、Bridge、MACVLAN、VLAN 等等。

`bridge` CNI plugin 的实现见 
[plugins/main/bridge/bridge.go](https://github.com/containernetworking/plugins/blob/v0.9.1/plugins/main/bridge/bridge.go)

执行逻辑如下：

1. 在默认 netns 创建一个 Linux bridge，这台宿主机上的所有容器都将连接到这个 bridge。
1. 创建一个 veth pair，将容器和 bridge 连起来。
1. 分配一个 IP 地址，配置到 pause 容器，设置路由。

    IP 从配套的网络服务 IPAM（IP Address Management）中分配的。最场景的 IPAM plugin 是
    `host-local`，它从预先设置的一个网段里分配一个 IP，并将状态信息写到宿主机的本地文件系统，因此重启不会丢失。
    `host-local` IPAM 的实现见 [plugins/ipam/host-local](https://github.com/containernetworking/plugins/tree/v0.9.1/plugins/ipam/host-local)。

1. 修改 `resolv.conf`，为容器配置 DNS。这里的 DNS 信息是从传给 CNI plugin 的参数中解析的。

以上过程完成之后，容器和宿主机（以及同宿主机的其他容器）之间的网络就通了，
CNI 插件会将结果以 JSON 返回给 kubelet。

### CNI 插件实现举例：Noop

再来看另一种**<mark>比较有趣的 CNI 插件</mark>**：`noop`。这个插件是 CNI 项目自带的，
代码见 [plugins/test/noop/main.go](https://github.com/containernetworking/cni/blob/v0.8.1/plugins/test/noop/main.go#L184)。

```go
func cmdAdd(args *skel.CmdArgs) error {
    return debugBehavior(args, "ADD")
}

func cmdDel(args *skel.CmdArgs) error {
    return debugBehavior(args, "DEL")
}
```

从名字以及以上代码可以看出，这个 CNI 插件（几乎）什么事情都不做。用途：

1. **<mark>测试或调试</mark>**：它可以打印 debug 信息。
2. 给**<mark>只支持 hostNetwork 的节点</mark>**使用。

    每个 node 上必须有一个配置正确的 CNI 插件，kubelet 自检才能通过，否则 node 会处于 NotReady 状态。

    某些情况下，我们不想让一些 node（例如 master node）承担正常的、创建带 IP pod 的工作，
    只要它能创建 hostNetwork 类型的 pod 就行了（这样就无需给这些 node 分配 PodCIDR，
    也不需要在 node 上启动 IPAM 服务）。

    这种情况下，就可以用 noop 插件。参考配置：

    ```shell
    $ cat /etc/cni/net.d/98-noop.conf
    {
        "cniVersion": "0.3.1",
        "type": "noop"
    }
    ```

### CNI 插件实现举例：Cilium

这个就很复杂了，做的事情非常多，可参考 [Cilium Code Walk Through: CNI Create Network]({% link _posts/2019-06-17-cilium-code-cni-create-network.md %})。

## 6.5 为容器配置跨节点通信网络（inter-host networking）

这项工作**<mark>不在 K8s 及 CNI 插件的职责范围内</mark>**，是由具体网络方案
在节点上的 agent 完成的，例如 flannel 网络的 flanneld，cilium 网络的 cilium-agent。

简单来说，跨节点通信有两种方式：

1. 隧道（tunnel or overlay）
2. 直接路由

这里赞不展开，可参考 [迈入 Cilium+BGP 的云原生网络时代]({% link _posts/2020-11-04-trip-stepping-into-cloud-native-networking-era-zh.md %})。

## 6.6 创建 `init` 容器及业务容器

至此，网络部分都配置好了。接下来就开始**<mark>启动真正的业务容器</mark>**。

Sandbox 容器初始化完成后，kubelet 就开始创建其他容器。
首先会启动 `PodSpec` 中指定的所有 init 容器，
[代码](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/kubelet/kuberuntime/kuberuntime_manager.go#L690)
然后才启动主容器（main containers）。

### 调用栈概览

```
startContainer
 |-m.runtimeService.CreateContainer                      // pkg/kubelet/cri/remote/remote_runtime.go
 |  |-r.runtimeClient.CreateContainer                    // -> pkg/kubelet/dockershim/docker_container.go
 |       |-new(CreateContainerResponse)                  // staging/src/k8s.io/cri-api/pkg/apis/runtime/v1/api.pb.go
 |       |-Invoke("/runtime.v1.RuntimeService/CreateContainer")
 |
 |  CreateContainer // pkg/kubelet/dockershim/docker_container.go
 |      |-ds.client.CreateContainer                      // -> pkg/kubelet/dockershim/libdocker/instrumented_client.go
 |             |-d.client.ContainerCreate                // -> vendor/github.com/docker/docker/client/container_create.go
 |                |-cli.post("/containers/create")
 |                |-json.NewDecoder().Decode(&resp)
 |
 |-m.runtimeService.StartContainer(containerID)          // -> pkg/kubelet/cri/remote/remote_runtime.go
    |-r.runtimeClient.StartContainer
         |-new(CreateContainerResponse)                  // staging/src/k8s.io/cri-api/pkg/apis/runtime/v1/api.pb.go
         |-Invoke("/runtime.v1.RuntimeService/StartContainer")
```

### 具体过程

```go
// pkg/kubelet/kuberuntime/kuberuntime_container.go

func (m *kubeGenericRuntimeManager) startContainer(podSandboxID, podSandboxConfig, spec *startSpec, pod *v1.Pod,
     podStatus *PodStatus, pullSecrets []v1.Secret, podIP string, podIPs []string) (string, error) {

    container := spec.container

    // Step 1: 拉镜像
    m.imagePuller.EnsureImageExists(pod, container, pullSecrets, podSandboxConfig)

    // Step 2: 通过 CRI 创建容器
    containerConfig := m.generateContainerConfig(container, pod, restartCount, podIP, imageRef, podIPs, target)

    m.internalLifecycle.PreCreateContainer(pod, container, containerConfig)
    containerID := m.runtimeService.CreateContainer(podSandboxID, containerConfig, podSandboxConfig)
    m.internalLifecycle.PreStartContainer(pod, container, containerID)

    // Step 3: 启动容器
    m.runtimeService.StartContainer(containerID)

    legacySymlink := legacyLogSymlink(containerID, containerMeta.Name, sandboxMeta.Name, sandboxMeta.Namespace)
    m.osInterface.Symlink(containerLog, legacySymlink)

    // Step 4: 执行 post start hook
    m.runner.Run(kubeContainerID, pod, container, container.Lifecycle.PostStart)
}
```

过程：

1. [拉镜像](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/kubelet/kuberuntime/kuberuntime_container.go#L140)。
  如果是私有镜像仓库，就会从 PodSpec 中寻找访问仓库用的 secrets。

1. 通过 CRI [创建 container](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/kubelet/kuberuntime/kuberuntime_container.go#L179)。

    从 parent PodSpec 的 `ContainerConfig` struct 中解析参数（command, image, labels, mounts, devices, env variables 等等），
    然后通过 protobuf 发送给 CRI plugin。例如对于 docker，收到请求后会反序列化，从中提取自己需要的参数，然后发送给 Daemon API。
    过程中它会给容器添加几个 metadata labels （例如 container type, log path, sandbox ID）。

1. 然后通过 `runtimeService.startContainer()` 启动容器；
1. 如果注册了 post-start hooks，接下来就执行这些 hooks。**<mark>post Hook 类型</mark>**：

  * `Exec`：在容器内执行具体的 shell 命令。
  * `HTTP`：对容器内的服务（endpoint）发起 HTTP 请求。

  如果 PostStart hook 运行时间过长，或者 hang 住或失败了，容器就无法进入 `running` 状态。

# 7 结束

至此，应该已经有 3 个 pod 在运行了，取决于系统资源和调度策略，它们可能在一台
node 上，也可能分散在多台。
