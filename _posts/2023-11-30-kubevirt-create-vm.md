---
layout    : post
title     : "Spawn a Virtual Machine in Kubernetes with kubevirt: A Deep Dive (2023)"
date      : 2023-11-30
lastupdate: 2023-11-30
categories: k8s kubevirt vm
---

<p align="center"><img src="/assets/img/kubevirt-create-vm/kubevirt-arch.png" width="100%" height="100%"></p>
<p align="center">Fig. kubevirt architecture overview</p>

An introductory post before this deep dive:
[Virtual Machines on Kubernetes: Requirements and Solutions (2023)]({% link _posts/2023-11-29-vm-on-k8s.md %})

Based on kubevirt `v1.0.0`, `v1.1.0`.

----

* TOC
{:toc}

----

<p align="center"><img src="/assets/img/kubevirt-create-vm/kubevirt-arch.png" width="100%" height="100%"></p>
<p align="center">Fig. Architecture overview of the kubevirt solution</p>

This post assumes there is already a running Kubernetes cluster, and kubevirt
is correctly deployed in this cluster.

# 1 `virt-handler` startup

## 1.1 Agent responsibilities

As the node agent, `virt-handler` is responsible for
**<mark>managing the lifecycle of all VMs on that node</mark>**, such as creating, destroying,
pausing, ..., freezing those VMs. It functions similarly to
OpenStack's `nova-compute`, but with the added complexity of **<mark>running each VM
inside a Kubernetes Pod</mark>**, which requires collaboration with `kubelet` - Kubernete's node agent.
For example,

* When creating a VM, `virt-handler` must wait until kubelet creates the corresponding Pod,
* When destroying a VM, `virt-handler` handles the VM destruction first,
  followed by kubelet performing the remaining cleanup steps (destroying the Pod).

## 1.2 Start and initialization (call stack)

```
Run                                           // cmd/virt-handler/virt-handler.go
  |-vmController := NewController()
  |-vmController.Run()
      |-Run()                                 // pkg/virt-handler/vm.go
         |-go c.deviceManagerController.Run()
         | 
         |-for domain in c.domainInformer.GetStore().List() {
         |     d := domain.(*api.Domain)
         |     vmiRef := v1.NewVMIReferenceWithUUID(...)
         |     key := controller.VirtualMachineInstanceKey(vmiRef)
         | 
         |     exists := c.vmiSourceInformer.GetStore().GetByKey(key)
         |     if !exists
         |         c.Queue.Add(key)
         |-}
         | 
         |-for i := 0; i < threadiness; i++ // 10 goroutine by default
               go c.runWorker
                  /
      /----------/
     /
runWorker
  |-for c.Execute() {
         |-key := c.Queue.Get()
         |-c.execute(key) // handle VM changes
              |-vmi, vmiExists := d.getVMIFromCache(key)
              |-domain, domainExists, domainCachedUID := d.getDomainFromCache(key)
              |-if !vmiExists && string(domainCachedUID) != ""
              |     vmi.UID = domainCachedUID
              |-if string(vmi.UID) == "" {
              |     uid := virtcache.LastKnownUIDFromGhostRecordCache(key)
              |     if uid != "" {
              |         vmi.UID = uid
              |     } else { // legacy support, attempt to find UID from watchdog file it exists.
              |         uid := watchdog.WatchdogFileGetUID(d.virtShareDir, vmi)
              |         if uid != ""
              |             vmi.UID = types.UID(uid)
              |     }
              |-}
              |-return d.defaultExecute(key, vmi, vmiExists, domain, domainExists)
    }
```

Steps done during `virt-handler` boostrap:

1. **<mark>Start necessary controllers</mark>**, such as the device-related controller.
2. **<mark>Scan all VMs on the node</mark>** and perform any necessary cleanups.
3. **<mark>Spawn goroutines to handle VM-related tasks</mark>**.

    Each goroutine runs an infinite loop, monitoring changes to kubevirt's VMI (Virtual
    Machine Instance) custom resources and responding accordingly. This includes
    actions like creating, deleting, ..., unpausing VMs. For example, if a
    new VM is detected to be created on the node, the goroutine will initiate the
    creation process.

## 1.3 Summary

Now that the agent is ready to handle VM-related tasks, let's create a VM in this
Kubernetes cluster and see what happens in the behind.

# 2 Create a `VirtualMachine` in Kubernetes

Let's see how to create a KVM-based virtual machine (just like the ones you've created in OpenStack,
or the EC2 instances you're using on public clouds) with kubevirt, and what happens in the behind.

<p align="center"><img src="/assets/img/vm-on-k8s/kubevirt-workflow.png" width="75%" height="75%"></p>
<p align="center">Fig. Workflow of creating a VM in kubevirt.
<mark>Left: steps added by kubevirt</mark>; Right: vanilla precedures of creating a Pod in k8s. [2]
</p>

## 2.1 `kube-apiserver`: create a `VirtualMachine` CR

kubevirt introduces a `VirtualMachine` CRD, which allows to define the
specifications of virtual machines, such as CPU, memory, network, and disk
configurations. 
Below is the spec of our to-be-created VM, it's ok if you don't undertand all the fields:

```yaml
apiVersion: kubevirt.io/v1
kind: VirtualMachine
metadata:
  name: kubevirt-smoke-fedora
spec:
  running: true
  template:
    metadata:
      annotations:
        kubevirt.io/keep-launcher-alive-after-failure: "true"
    spec:
      nodeSelector:
        kubevirt.io/schedulable: "true"
      architecture: amd64
      domain:
        clock:
          timer:
            hpet:
              present: false
            hyperv: {}
            pit:
              tickPolicy: delay
            rtc:
              tickPolicy: catchup
          utc: {}
        cpu:
          cores: 1
        resources:
          requests:
            memory: 4G
        machine:
          type: q35
        devices:
          interfaces:
          - bridge: {}
            name: default
          disks:
          - disk:
              bus: virtio
            name: containerdisk
          - disk:
              bus: virtio
            name: emptydisk
          - disk:
              bus: virtio
            name: cloudinitdisk
        features:
          acpi:
            enabled: true
        firmware:
          uuid: c3ecdb42-282e-44c3-8266-91b99ac91261
      networks:
      - name: default
        pod: {}
      volumes:
      - containerDisk:
          image: kubevirt/fedora-cloud-container-disk-demo:latest
          imagePullPolicy: Always
        name: containerdisk
      - emptyDisk:
          capacity: 2Gi
        name: emptydisk
      - cloudInitNoCloud:
          userData: |-
            #cloud-config
            password: changeme               # password of this VM
            chpasswd: { expire: False }
        name: cloudinitdisk
```

Now just apply it:

```shell
(master) $ k apply -f kubevirt-smoke-fedora.yaml
```

## 2.2 `virt-controller`: translate `VirtualMachine` to `VirtualMachineInstance` and `Pod`

`virt-controller`, a control plane component of kubevirt, monitors
`VirtualMachine` CRs/objects and generates corresponding `VirtualMachineInstance`
objects, and further creates a standard Kubernetes Pod object to describe the VM.
See [renderLaunchManifest()](https://github.com/kubevirt/kubevirt/blob/v1.1.0/pkg/virt-controller/services/template.go#L320)
for details.

> `VirtualMachineInstance` is a running instance of the corresponding VirtualMachine,
> such as, if you stop a VirtualMachine, the corresponding `VirtualMachineInstance` will be deleted,
> but it will be recreated after you start this VirtualMachine again.

```shell
$ k get vm
NAME                    AGE    STATUS    READY
kubevirt-smoke-fedora   ...

$ k get vmi
NAME                    AGE     PHASE     IP             NODENAME         READY   LIVE-MIGRATABLE   PAUSED
kubevirt-smoke-fedora   ...

$ k get pod -o wide | grep fedora
virt-launcher-kubevirt-smoke-fedora-2kx25   <status> ...
```

Once the Pod object is created, `kube-scheduler` takes over and selects a suitable
node for the Pod. This has no differences compared with scheduling a normal Kubernetes pod.

The Pod's yaml specification is very lengthy, we'll see them piece by piece in following
sections.

## 2.3 `kube-scheduler`: schedule `Pod`

Based on Pod's label selectors, `kube-scheduler` will choose a node for the Pod, then
update the pod spec.

<p align="center"><img src="/assets/img/kubevirt-create-vm/kubevirt-arch.png" width="100%" height="100%"></p>
<p align="center">Fig. Architecture overview of the kubevirt solution</p>

The steps described above, from applying a `VirtualMachine` CR to the scheduling
of the corresponding Pod on a node, all occur within the master node or control
plane. Subsequent steps involve happen within the selected node.

## 2.4 `kubelet`: create `Pod`

Upon detecting a Pod has been scheduled to this node, `kubelet` on that node initiates the
creation of the Pod using its specifications.

While a standard Pod typically
consists of a `pause` container for holding namespaces and a main container for
executing user-defined tasks, Kubernetes also allows for multiple containers to
be included within a single Pod. This is particularly useful in scenarios such
as service mesh, where a sidecar container can be injected into each Pod to
process network requests.

In the case of kubevirt, this "multi-container"
property is leveraged even further. `virt-controller` described 4 containers within the Pod:

* 2 init containers for creating shared directories for containers in this Pod and copying files;
* 1 volume container for holding volumes;
* 1 compute container for **<mark>holding the VM</mark>** in this Pod.

### 2.4.1 `pause` container

`crictl ps` won't show the pause container, but we can check it with `ps`:

```shell
(node) $ ps -ef | grep virt-launcher
qemu     822447 821556  /usr/bin/virt-launcher-monitor --qemu-timeout 288s --name kubevirt-smoke-fedora --uid 413e131b-408d-4ec6-9d2c-dc691e82cfda --namespace default --kubevirt-share-dir /var/run/kubevirt --ephemeral-disk-dir /var/run/kubevirt-ephemeral-disks --container-disk-dir /var/run/kubevirt/container-disks --grace-period-seconds 45 --hook-sidecars 0 --ovmf-path /usr/share/OVMF --run-as-nonroot --keep-after-failure
qemu     822464 822447  /usr/bin/virt-launcher         --qemu-timeout 288s --name kubevirt-smoke-fedora --uid 413e131b-408d-4ec6-9d2c-dc691e82cfda --namespace default --kubevirt-share-dir /var/run/kubevirt --ephemeral-disk-dir /var/run/kubevirt-ephemeral-disks --container-disk-dir /var/run/kubevirt/container-disks --grace-period-seconds 45 --hook-sidecars 0 --ovmf-path /usr/share/OVMF --run-as-nonroot
qemu     822756 822447  /usr/libexec/qemu-kvm -name ... # parent is virt-launcher-monitor

(node) $ ps -ef | grep pause
root     820808 820788  /pause
qemu     821576 821556  /pause
...
```

Process start information:

```shell
$ cat /proc/821576/cmdline | tr '\0' ' ' # the `pause` process
/pause

$ cat /proc/821556/cmdline | tr '\0' ' ' # the parent process
/usr/bin/containerd-shim-runc-v2 -namespace k8s.io -id 09c4b -address /run/containerd/containerd.sock
```

### 2.4.2 `1st` init container: install `container-disk-binary` to Pod

Snippet from Pod yaml:

```yaml
  initContainers:
  - command:
    - /usr/bin/cp
    - /usr/bin/container-disk
    - /init/usr/bin/container-disk
    env:
    - name: XDG_CACHE_HOME
      value: /var/run/kubevirt-private
    - name: XDG_CONFIG_HOME
      value: /var/run/kubevirt-private
    - name: XDG_RUNTIME_DIR
      value: /var/run
    image: virt-launcher:v1.0.0
    name: container-disk-binary
    resources:
      limits:
        cpu: 100m
        memory: 40M
      requests:
        cpu: 10m
        memory: 1M
    securityContext:
      allowPrivilegeEscalation: true
      capabilities:
        drop:
        - ALL
      privileged: true
      runAsGroup: 107
      runAsNonRoot: false
      runAsUser: 107
    volumeMounts:
    - mountPath: /init/usr/bin
      name: virt-bin-share-dir
```

It copies a binary named `container-disk` from container image to a directory of the Pod,

> Source code [cmd/container-disk/main.c](https://github.com/kubevirt/kubevirt/blob/v1.1.0/cmd/container-disk-v2alpha/main.c).
> About ~100 lines of C code.

so this binary is **<mark>shared among all containers of this Pod</mark>**.
**<mark><code>virt-bin-share-dir</code></mark>** is declared as a Kubernetes
[emptyDir](https://kubernetes.io/docs/concepts/storage/volumes/#emptydir),
kubelet will create a volume for it automatically in local disk:

> For a Pod that defines an emptyDir volume, the volume is created when the Pod
> is assigned to a node. As the name says, the emptyDir volume is initially
> empty. All containers in the Pod can read and write the same files in the
> emptyDir volume, though that volume can be mounted at the same or different
> paths in each container. When a Pod is removed from a node for any reason, the
> data in the emptyDir is deleted permanently.

Check the container:

```shell
$ crictl ps -a | grep container-disk-binary # init container runs and exits
55f4628feb5a0   Exited   container-disk-binary   ...
```

Check the `emptyDir` created for it:

```shell
$ crictl inspect 55f4628feb5a0
    ...
    "mounts": [
      {
        "containerPath": "/init/usr/bin",
        "hostPath": "/var/lib/k8s/kubelet/pods/8364158c/volumes/kubernetes.io~empty-dir/virt-bin-share-dir",
      },
```

Check **<mark>what's inside the directory</mark>**:

```shell
$ ls /var/lib/k8s/kubelet/pods/8364158c/volumes/kubernetes.io~empty-dir/virt-bin-share-dir
container-disk # an excutable that will be used by the other containers in this Pod
```

### 2.4.3 `2nd` init container: `volumecontainerdisk-init`

```yaml
  - command:
    - /usr/bin/container-disk
    args:
    - --no-op                   # exit(0) directly
    image: kubevirt/fedora-cloud-container-disk-demo:latest
    name: volumecontainerdisk-init
    resources:
      limits:
        cpu: 10m
        memory: 40M
      requests:
        cpu: 1m
        ephemeral-storage: 50M
        memory: 1M
    securityContext:
      allowPrivilegeEscalation: true
      capabilities:
        drop:
        - ALL
      privileged: true
      runAsNonRoot: false
      runAsUser: 107
    volumeMounts:
    - mountPath: /var/run/kubevirt-ephemeral-disks/container-disk-data/413e131b-408d-4ec6-9d2c-dc691e82cfda
      name: container-disks
    - mountPath: /usr/bin
      name: virt-bin-share-dir
```

With `--no-op` option, the `container-disk` program will exit
immediately with a return code of `0`, indicating success.

So, what is the purpose of this container? It appears that it references a volume named
`container-disks`, suggesting that it uses this approach as a workaround for
certain edge cases. This ensures that the directory (emptyDir) is created
before being utilized by the subsequent container.

### 2.4.4 `1st` main container: `volumecontainerdisk`

```yaml
  - command:
    - /usr/bin/container-disk
    args:
    - --copy-path
    - /var/run/kubevirt-ephemeral-disks/container-disk-data/413e131b-408d-4ec6-9d2c-dc691e82cfda/disk_0
    image: kubevirt/fedora-cloud-container-disk-demo:latest
    name: volumecontainerdisk
    resources:                         # needs little CPU & memory
      limits:
        cpu: 10m
        memory: 40M
      requests:
        cpu: 1m
        ephemeral-storage: 50M
        memory: 1M
    securityContext:
      allowPrivilegeEscalation: true
      capabilities:
        drop:
        - ALL
      privileged: true
      runAsNonRoot: false
      runAsUser: 107
    volumeMounts:
    - mountPath: /usr/bin
      name: virt-bin-share-dir
    - mountPath: /var/run/kubevirt-ephemeral-disks/container-disk-data/413e131b-408d-4ec6-9d2c-dc691e82cfda
      name: container-disks
```

This container uses two directories created by the init containers:

1. `virt-bin-share-dir`: an emptyDir, created by the 1st init container;
1. `container-disks`: an emptyDir, created by the 2nd init container;

`--copy-path <path>`:

* Create this path is not exist;
* Create a unix domain socket, listen requests and close them;

It seems that this container serves the purpose of holding the
`container-disk-data` volume and does not perform any other significant tasks.

### 2.4.5 `2nd` main container: `compute`

```yaml
  - command:
    - /usr/bin/virt-launcher-monitor
    - --qemu-timeout
    - 288s
    - --name
    - kubevirt-smoke-fedora
    - --uid
    - 413e131b-408d-4ec6-9d2c-dc691e82cfda
    - --namespace
    - default
    - --kubevirt-share-dir
    - /var/run/kubevirt
    - --ephemeral-disk-dir
    - /var/run/kubevirt-ephemeral-disks
    - --container-disk-dir
    - /var/run/kubevirt/container-disks
    - --grace-period-seconds
    - "45"
    - --hook-sidecars
    - "0"
    - --ovmf-path
    - /usr/share/OVMF
    - --run-as-nonroot
    - --keep-after-failure
    env:
    - name: XDG_CACHE_HOME
      value: /var/run/kubevirt-private
    - name: XDG_CONFIG_HOME
      value: /var/run/kubevirt-private
    - name: XDG_RUNTIME_DIR
      value: /var/run
    - name: VIRT_LAUNCHER_LOG_VERBOSITY
      value: "6"
    - name: LIBVIRT_DEBUG_LOGS
      value: "1"
    - name: VIRTIOFSD_DEBUG_LOGS
      value: "1"
    - name: POD_NAME
      valueFrom:
        fieldRef:
          apiVersion: v1
          fieldPath: metadata.name
    image: virt-launcher:v1.0.0
    name: compute
    resources:
      limits:
        devices.kubevirt.io/kvm: "1"
        devices.kubevirt.io/tun: "1"
        devices.kubevirt.io/vhost-net: "1"
      requests:
        cpu: 100m
        devices.kubevirt.io/kvm: "1"
        devices.kubevirt.io/tun: "1"
        devices.kubevirt.io/vhost-net: "1"
        ephemeral-storage: 50M
        memory: "4261567892"
    securityContext:
      allowPrivilegeEscalation: true
      capabilities:
        add:
        - NET_BIND_SERVICE
        drop:
        - ALL
      privileged: true
      runAsGroup: 107
      runAsNonRoot: false
      runAsUser: 107
    volumeMounts:
    - mountPath: /var/run/kubevirt-private
      name: private
    - mountPath: /var/run/kubevirt
      name: public
    - mountPath: /var/run/kubevirt-ephemeral-disks
      name: ephemeral-disks
    - mountPath: /var/run/kubevirt/container-disks
      mountPropagation: HostToContainer
      name: container-disks
    - mountPath: /var/run/libvirt
      name: libvirt-runtime
    - mountPath: /var/run/kubevirt/sockets
      name: sockets
    - mountPath: /var/run/kubevirt/hotplug-disks
      mountPropagation: HostToContainer
      name: hotplug-disks
    - mountPath: /var/run/kubevirt-ephemeral-disks/disk-data/containerdisk
      name: local
```

This container runs a binary called **<mark><code>virt-launcher-monitor</code></mark>**,
which is **<mark>a simple wrapper</mark>** around `virt-launcher`.
The main purpose of adding a wrapping layer is for better cleaning up when process exits.

#### `virt-launcher-monitor`

All `virt-launcher-monitor`'s arguments will be passed to `virt-launcher` changed, except
`--keep-after-failure` will be removed - this is a monitor-only flag.

```go
// run virt-launcher process and monitor it to give qemu an extra grace period to properly terminate in case of crashes
func RunAndMonitor(containerDiskDir string) (int, error) {
    args := removeArg(os.Args[1:], "--keep-after-failure")

    cmd := exec.Command("/usr/bin/virt-launcher", args...)
    cmd.SysProcAttr = &syscall.SysProcAttr{
        AmbientCaps: []uintptr{unix.CAP_NET_BIND_SERVICE},
    }
    cmd.Start()

    sigs := make(chan os.Signal, 10)
    signal.Notify(sigs, syscall.SIGINT, syscall.SIGTERM, syscall.SIGQUIT, syscall.SIGCHLD)
    go func() {
        for sig := range sigs {
            switch sig {
            case syscall.SIGCHLD:
                var wstatus syscall.WaitStatus
                wpid := syscall.Wait4(-1, &wstatus, syscall.WNOHANG, nil)

                log.Log.Infof("Reaped pid %d with status %d", wpid, int(wstatus))
                if wpid == cmd.Process.Pid {
                    exitStatus <- wstatus.ExitStatus()
                }
            default: // Log("signalling virt-launcher to shut down")
                cmd.Process.Signal(syscall.SIGTERM)
                sig.Signal()
            }
        }
    }()

    exitCode := <-exitStatus // wait for VM's exit
    // do cleanups here
}
```

#### `virt-launcher` call stack: start `virtqemud/cmdserver`

```
main // cmd/virt-launcher/virt-launcher.go
  |-NewLibvirtWrapper(*runWithNonRoot)
  |-SetupLibvirt(libvirtLogFilters)
  |-StartVirtquemud(stopChan)
  |    |-go func() {
  |    |     for {
  |    |         Run("/usr/sbin/virtqemud -f /var/run/libvirt/virtqemud.conf")
  |    |
  |    |         select {
  |    |         case <-stopChan:
  |    |             return cmd.Process.Kill()
  |    |         }
  |    |     }
  |    |-}()
  |
  |-domainConn := createLibvirtConnection() // "qemu+unix:///session?socket=/var/run/libvirt/virtqemud-sock" or "qemu:///system"
  |
  |-notifier := notifyclient.NewNotifier(*virtShareDir)
  |-domainManager := NewLibvirtDomainManager()
  |
  |-startCmdServer("/var/run/kubevirt/sockets/launcher-init-sock")
  |-startDomainEventMonitoring
  |-domain := waitForDomainUUID()
  |
  |-mon := virtlauncher.NewProcessMonitor(domainName,)
  |-mon.RunForever()
        |-monitorLoop()
```

It starts two processes inside the container:

1. **<mark><code>virtqemud</code></mark>**: a libvirt component, runs as a daemon process;
2. **<mark><code>cmdserver: a gRPC server</code></mark>**, provides VM operation (delete/pause/freeze/...) interfaces to the caller;

#### `virtqemud`: management for QEMU VMs

[virtqemud](https://libvirt.org/manpages/virtqemud.html) is a server side daemon component of the libvirt virtualization management system,

* one of a collection of modular daemons that replace functionality previously provided by the monolithic libvirtd daemon.
* provide management for QEMU virtual machines.
* listens for requests on a **<mark>local Unix domain socket</mark>** by default. Remote access via TLS/TCP and backwards compatibility with legacy
  clients expecting libvirtd is provided by the virtproxyd daemon.

Check the container that will hold the VM:

```shell
$ crictl ps -a | grep compute
f67f57d432534       Running     compute     0       09c4b63f6bca5       virt-launcher-kubevirt-smoke-fedora-2kx25
```

Configurations of `virtqemud`:

```shell
$ crictl exec -it f67f57d432534 sh
sh-5.1$ cat /var/run/libvirt/virtqemud.conf
listen_tls = 0
listen_tcp = 0
log_outputs = "1:stderr"
log_filters="3:remote 4:event 3:util.json 3:util.object 3:util.dbus 3:util.netlink 3:node_device 3:rpc 3:access 3:util.threadjob 3:cpu.cpu 3:qemu.qemu_monitor 1:*"
```

#### Process tree

There are several processes inside the `compute` container, show their relationships
with **<mark><code>pstree</code></mark>**:

```shell
$ pstree -p <virt-launcher-monitor pid> --hide-threads
virt-launcher-m(<pid>)─┬─qemu-kvm(<pid>) # The real VM process, we'll see this in the next chapter
                       └─virt-launcher(<pid>)─┬─virtlogd(<pid>)
                                              └─virtqemud(<pid>)

# Show the entire process arguments
$ pstree -p <virt-launcher-monitor pid> --hide-threads --arguments
$ virt-launcher-monitor --qemu-timeout 321s --name kubevirt-smoke-fedora ...
  ├─qemu-kvm            -name guest=default_kubevirt-smoke-fedora,debug-threads=on ...
  └─virt-launcher       --qemu-timeout 321s --name kubevirt-smoke-fedora ...
      ├─virtlogd        -f /etc/libvirt/virtlogd.conf
      └─virtqemud       -f /var/run/libvirt/virtqemud.conf
```

## 2.5 `virt-launcher`: reconcile VM state (create VM in this case)

Status til now:

<p align="center"><img src="/assets/img/kubevirt-create-vm/before-vm-creation.png" width="65%" height="65%"></p>
<p align="center">Fig. <mark>Ready to create a KVM VM inside the Pod</mark></p>

1. `kubelet` has successfully created a Pod and is reconciling the Pod status based on the Pod specification.

    Note that certain details, such as network creation for the Pod, have been omitted to keep this post concise.
    There are no differences from normal Pods.

2. `virt-handler` is prepared to synchronize the status of the
   `VirtualMachineInstance` with a real KVM virtual machine on this node. As
   there is currently no virtual machine present, the first task of the
   `virt-handler` is to **<mark>create the virtual machine</mark>**.

Now, let's delve into the detailed steps involved in creating a KVM virtual machine.

### 2.5.1 `virt-handler/cmdclient -> virt-launcher/cmdserver`: sync VMI

An informer is used in `virt-handler` to sync VMI, it will call to the following stack:

```
defaultExecute
  |-switch {
    case shouldShutdown:
        d.processVmShutdown(vmi, domain)
    case shouldDelete:
        d.processVmDelete(vmi)
    case shouldCleanUp:
        d.processVmCleanup(vmi)
    case shouldUpdate:
        d.processVmUpdate(vmi, domain)
          |// handle migration if needed
          |
          |// handle vm create
          |-d.vmUpdateHelperDefault
               |-client.SyncVirtualMachine(vmi, options)
                   |-// lots of preparation work here
                   |-genericSendVMICmd("SyncVMI", c.v1client.SyncVirtualMachine, vmi, options)
  }
```

[`client.SyncVirtualMachine(vmi, options)`]() does lots of preparation work, then
calls `SyncVMI()` gRPC method to synchronize VM status - if not exist, then create it.
This method will be handled by the `cmdserver` in `virt-launcher`.

### 2.5.2 `virt-launcher/cmdserver: SyncVirtualMachine()` `->` libvirt C API `virDomainCreateWithFlags()`

```
SyncVirtualMachine // pkg/virt-launcher/virtwrap/cmd-server/server.go
  |-vmi, response := getVMIFromRequest(request.Vmi)
  |-domainManager.SyncVMI(vmi, l.allowEmulation, request.Options)
      |-domain := &api.Domain{}
      |-c := l.generateConverterContext // generate libvirt domain from VMI spec
      |-dom := l.virConn.LookupDomainByName(domain.Spec.Name)
      |-if notFound {
      |     domain = l.preStartHook(vmi, domain, false)
      |     dom = withNetworkIfacesResources(
      |         vmi, &domain.Spec,
      |         func(v *v1.VirtualMachineInstance, s *api.DomainSpec) (cli.VirDomain, error) {
      |             return l.setDomainSpecWithHooks(v, s)
      |         },
      |     )
      |
      |     l.metadataCache.UID.Set(vmi.UID)
      |     l.metadataCache.GracePeriod.Set( api.GracePeriodMetadata{DeletionGracePeriodSeconds: converter.GracePeriodSeconds(vmi)},)
      |     logger.Info("Domain defined.")
      |-}
      | 
      |-switch domState {
      |     case vm create:
      |         l.generateCloudInitISO(vmi, &dom)
      |         dom.CreateWithFlags(getDomainCreateFlags(vmi)) // start VirtualMachineInstance
      |     case vm pause/unpause:
      |     case disk attach/detach/resize disks:
      |     case hot plug/unplug virtio interfaces:
      |-}
```

As the above code shows, it eventually calls into **<mark><code>libvirt API</code></mark>**
to create a **<mark><code>"domain"</code></mark>**.

> https://unix.stackexchange.com/questions/408308/why-are-vms-in-kvm-qemu-called-domains 
>
> They're not kvm exclusive terminology (xen also refers to machines as
> domains). <mark>A hypervisor is a rough equivalent to domain zero</mark>, or dom0, which
> is the first system initialized on the kernel and has special privileges.
> Other domains started later are called domU and are the equivalent to a guest
> system or virtual machine.  The reason is probably that both are very similar
> as they are executed on the kernel that handles them.

### 2.5.3 `libvirt API -> virtqemud`: create domain (VM)

#### `LibvirtDomainManager`

All VM/VMI operations are abstracted into a `LibvirtDomainManager` struct:

```
// pkg/virt-launcher/virtwrap/manager.go

type LibvirtDomainManager struct {
    virConn cli.Connection

    // Anytime a get and a set is done on the domain, this lock must be held.
    domainModifyLock sync.Mutex
    // mutex to control access to the guest time context
    setGuestTimeLock sync.Mutex

    credManager *accesscredentials.AccessCredentialManager

    hotplugHostDevicesInProgress chan struct{}
    memoryDumpInProgress         chan struct{}

    virtShareDir             string
    ephemeralDiskDir         string
    paused                   pausedVMIs
    agentData                *agentpoller.AsyncAgentStore
    cloudInitDataStore       *cloudinit.CloudInitData
    setGuestTimeContextPtr   *contextStore
    efiEnvironment           *efi.EFIEnvironment
    ovmfPath                 string
    ephemeralDiskCreator     ephemeraldisk.EphemeralDiskCreatorInterface
    directIOChecker          converter.DirectIOChecker
    disksInfo                map[string]*cmdv1.DiskInfo
    cancelSafetyUnfreezeChan chan struct{}
    migrateInfoStats         *stats.DomainJobInfo

    metadataCache *metadata.Cache
}
```

#### libvirt C API

```go
// vendor/libvirt.org/go/libvirt/domain.go

// See also https://libvirt.org/html/libvirt-libvirt-domain.html#virDomainCreateWithFlags
func (d *Domain) CreateWithFlags(flags DomainCreateFlags) error {
	C.virDomainCreateWithFlagsWrapper(d.ptr, C.uint(flags), &err)
}
```

### 2.5.4 virtqemud -> KVM subsystem

Create VM with xml spec.

The domain (VM) will be created, and the **<mark>VCPU will enter running state</mark>**
unless special flags are specified.

## 2.6 Recap

<p align="center"><img src="/assets/img/kubevirt-create-vm/vm-is-created.png" width="65%" height="65%"></p>
<p align="center">Fig. <mark>A KVM VM is created inside the Pod</mark></p>

```shell
$ crictl ps -a | grep kubevirt
960d3e86991fa     Running     volumecontainerdisk        0   09c4b63f6bca5       virt-launcher-kubevirt-smoke-fedora-2kx25
f67f57d432534     Running     compute                    0   09c4b63f6bca5       virt-launcher-kubevirt-smoke-fedora-2kx25
e8b79067667b7     Exited      volumecontainerdisk-init   0   09c4b63f6bca5       virt-launcher-kubevirt-smoke-fedora-2kx25
55f4628feb5a0     Exited      container-disk-binary      0   09c4b63f6bca5       virt-launcher-kubevirt-smoke-fedora-2kx25
```

This finishes our journey of creating a VM in kubevirt.

What it looks like if we created two kubevir `VirtualMachine`s and they are scheduled to the same node:

<p align="center"><img src="/assets/img/kubevirt-create-vm/node-topo.png" width="65%" height="65%"></p>
<p align="center">Fig. <mark>Two KVM VMs on the node</mark></p>

# 3 Management VM states

**<mark>Control path</mark>** of VM state changes:

```
kube-apiserver -> virt-handler -> cmdserver -> virtqemud -> KVM subsystem -> VM

 VMI states         VM agent     |-- virt-launcher pod --|     kernel
```

## 3.1 Resize CPU/Memory/Disk

Workflow (non hotplug):

1. `/VirtualMachine/VirtualMachineInstance` spec is modified;
2. `virt-handler` receives the changes and modifies KVM VM configurations via `virtqemud->KVM`;
3. Restart pod and KVM VM to make the changes take effect.

If hotplug is supported (e.g. Kubernetes VPA is supported), kubevirt should be able
to hot-reload the changes.

## 3.2 Delete VM

Similar workflow as the above.

# 4 Summary

This post illustrates what happens in the underlying when user
creates a `VirtualMachine` in Kubernetes with `kubevirt`.

# References

1. [github.com/kubevirt](https://github.com/kubevirt/kubevirt)
2. [Virtual Machines on Kubernetes: Requirements and Solutions (2023)]({% link _posts/2023-11-29-vm-on-k8s.md %})

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
