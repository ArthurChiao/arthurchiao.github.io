---
layout    : post
title     : "源码解析：K8s 创建 pod 时，背后发生了什么（三）（2021）"
date      : 2021-06-01
lastupdate: 2021-06-01
categories: k8s
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

# 2 kube-apiserver

请求从客户端发出后，便来到服务端，也就是 kube-apiserver。

## 2.0 调用栈概览

```

buildGenericConfig
  |-genericConfig = genericapiserver.NewConfig(legacyscheme.Codecs)  // cmd/kube-apiserver/app/server.go

NewConfig       // staging/src/k8s.io/apiserver/pkg/server/config.go
 |-return &Config{
      Serializer:             codecs,
      BuildHandlerChainFunc:  DefaultBuildHandlerChain,
   }                          /
                            /
                          /
                        /
DefaultBuildHandlerChain       // staging/src/k8s.io/apiserver/pkg/server/config.go
 |-handler := filterlatency.TrackCompleted(apiHandler)
 |-handler = genericapifilters.WithAuthorization(handler)
 |-handler = genericapifilters.WithAudit(handler)
 |-handler = genericapifilters.WithAuthentication(handler)
 |-return handler


WithAuthentication
 |-withAuthentication
    |-resp, ok := AuthenticateRequest(req)
    |  |-for h := range authHandler.Handlers {
    |      resp, ok := currAuthRequestHandler.AuthenticateRequest(req)
    |      if ok {
    |          return resp, ok, err
    |      }
    |    }
    |    return nil, false, utilerrors.NewAggregate(errlist)
    |
    |-audiencesAreAcceptable(apiAuds, resp.Audiences)
    |-req.Header.Del("Authorization")
    |-req = req.WithContext(WithUser(req.Context(), resp.User))
    |-return handler.ServeHTTP(w, req)

```

## 2.1 认证（Authentication）

kube-apiserver 首先会对请求进行**<mark>认证（authentication）</mark>**，以确保
用户身份是合法的（verify that the requester is who they say they are）。

具体过程：启动时，检查所有的 [命令行参数](https://kubernetes.io/docs/admin/kube-apiserver/)
，组织成一个 authenticator list，例如，

* 如果指定了 `--client-ca-file`，就会将 x509 证书加到这个列表；
* 如果指定了 `--token-auth-file`，就会将 token 加到这个列表；

不同 anthenticator 做的事情有所不同：

* [x509 handler](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/authentication/request/x509/x509.go#L60)
  验证该 HTTP 请求是用 TLS key 加密的，并且有 CA root 证书的签名。
* [bearer token handler](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/authentication/request/bearertoken/bearertoken.go#L38)
  验证请求中带的 token（HTTP Authorization 头中），在 apiserver 的 auth file 中是存在的（`--token-auth-file`）。
* [basicauth handler](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/plugin/pkg/authenticator/request/basicauth/basicauth.go#L37) 对 basic auth 信息进行校验。

**如果认证成功，就会将 `Authorization` 头从请求中删除**，然后在上下文中
[加上用户信息](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/endpoints/filters/authentication.go#L71-L75)。
这使得后面的步骤（例如鉴权和 admission control）能用到这里已经识别出的用户身份信息。

```go
// staging/src/k8s.io/apiserver/pkg/endpoints/filters/authentication.go

// WithAuthentication creates an http handler that tries to authenticate the given request as a user, and then
// stores any such user found onto the provided context for the request.
// On success, "Authorization" header is removed from the request and handler
// is invoked to serve the request.
func WithAuthentication(handler http.Handler, auth authenticator.Request, failed http.Handler,
    apiAuds authenticator.Audiences) http.Handler {
    return withAuthentication(handler, auth, failed, apiAuds, recordAuthMetrics)
}

func withAuthentication(handler http.Handler, auth authenticator.Request, failed http.Handler,
    apiAuds authenticator.Audiences, metrics recordMetrics) http.Handler {
    return http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
        resp, ok := auth.AuthenticateRequest(req) // 遍历所有 authenticator，任何一个成功就返回 OK
        if !ok {
            return failed.ServeHTTP(w, req)       // 所有认证方式都失败了
        }

        if !audiencesAreAcceptable(apiAuds, resp.Audiences) {
            fmt.Errorf("unable to match the audience: %v , accepted: %v", resp.Audiences, apiAuds)
            failed.ServeHTTP(w, req)
            return
        }

        req.Header.Del("Authorization") // 认证成功后，这个 header 就没有用了，可以删掉

        // 将用户信息添加到请求上下文中，供后面的步骤使用
        req = req.WithContext(WithUser(req.Context(), resp.User))
        handler.ServeHTTP(w, req)
    })
}
```

`AuthenticateRequest()` 实现：遍历所有 authenticator，任何一个成功就返回 OK，

```go
// staging/src/k8s.io/apiserver/pkg/authentication/request/union/union.go

func (authHandler *unionAuthRequestHandler) AuthenticateRequest(req) (*Response, bool) {
    for currAuthRequestHandler := range authHandler.Handlers {
        resp, ok := currAuthRequestHandler.AuthenticateRequest(req)
        if ok {
            return resp, ok, err
        }
    }

    return nil, false, utilerrors.NewAggregate(errlist)
}
```

## 2.2 鉴权（Authorization）

**<mark>发送者身份（认证）是一个问题，但他是否有权限执行这个操作（鉴权），是另一个问题</mark>**。
因此确认发送者身份之后，还需要进行鉴权。

鉴权的过程与认证非常相似，也是逐个匹配 authorizer 列表中的 authorizer：如果都失败了，
返回 `Forbidden` 并停止 [进一步处理](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/endpoints/filters/authorization.go#L60)。如果成功，就继续。

内置的 **<mark>几种 authorizer 类型</mark>**：

- [webhook](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/plugin/pkg/authorizer/webhook/webhook.go#L143)：
  与其他服务交互，验证是否有权限。
- [ABAC](https://github.com/kubernetes/kubernetes/blob/v1.21.0/pkg/auth/authorizer/abac/abac.go#L223)：
  根据**<mark>静态文件中规定的策略</mark>**（policies）来进行鉴权。
- [RBAC](https://github.com/kubernetes/kubernetes/blob/v1.21.0/plugin/pkg/auth/authorizer/rbac/rbac.go#L43)：
  根据 role 进行鉴权，其中 role 是 k8s 管理员提前配置的。
- [Node](https://github.com/kubernetes/kubernetes/blob/v1.21.0/plugin/pkg/auth/authorizer/node/node_authorizer.go#L67)：
  确保 node clients，例如 kubelet，只能访问本机内的资源。

要看它们的具体做了哪些事情，可以查看它们各自的 `Authorize()` 方法。

## 2.3 Admission control

至此，认证和鉴权都通过了。但这还没结束，K8s 中的**<mark>其它组件还需要对请求进行检查</mark>**，
其中就包括 [admission controllers](https://kubernetes.io/docs/admin/admission-controllers/#what-are-they)。

### 与鉴权的区别

* 鉴权（authorization）在前面，关注的是**用户是否有操作权限**，
* Admission controllers 在更后面，**<mark>对请求进行拦截和过滤，确保它们符合一些更广泛的集群规则和限制</mark>**，
  是**将请求对象持久化到 etcd 之前的最后堡垒**。

### 工作方式

* 与认证和鉴权类似，也是遍历一个列表，
* 但有一点核心区别：**<mark>任何一个 controller 检查没通过，请求就会失败</mark>**。

### 设计：可扩展

* 每个 controller 作为一个 plugin 存放在[`plugin/pkg/admission` 目录](https://github.com/kubernetes/kubernetes/tree/master/plugin/pkg/admission),
* 设计时已经考虑，只需要实现很少的几个接口
* 但注意，**<mark>admission controller 最终会编译到 k8s 的二进制文件</mark>**（而非独立的 plugin binary）

### 类型

Admission controllers 通常按不同目的分类，包括：**资源管理、安全管理、默认值管
理、引用一致性**（referential consistency）等类型。

例如，下面是资源管理类的几个 controller：

* `InitialResources`：为容器设置默认的资源限制（基于过去的使用量）；
* `LimitRanger`：为容器的 requests and limits 设置默认值，或对特定资源设置上限（例如，内存默认 512MB，最高不超过 2GB）。
* `ResourceQuota`：资源配额。

# 3 写入 etcd

至此，K8s 已经完成对请求的验证，允许它进行接下来的处理。

kube-apiserver 将**<mark>对请求进行反序列化，构造 runtime objects</mark>**（
kubectl generator 的反过程），并将它们**<mark>持久化到 etcd</mark>**。下面详细
看这个过程。

## 3.0 调用栈概览

对于本文创建 pod 的请求，相应的入口是
[POST handler](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/endpoints/installer.go#L815)
，它又会进一步将请求委托给一个创建具体资源的 handler。

```
registerResourceHandlers // staging/src/k8s.io/apiserver/pkg/endpoints/installer.go
 |-case POST:
```

```go
// staging/src/k8s.io/apiserver/pkg/endpoints/installer.go

        switch () {
        case "POST": // Create a resource.
            var handler restful.RouteFunction
            if isNamedCreater {
                handler = restfulCreateNamedResource(namedCreater, reqScope, admit)
            } else {
                handler = restfulCreateResource(creater, reqScope, admit)
            }

            handler = metrics.InstrumentRouteFunc(action.Verb, group, version, resource, subresource, .., handler)
            article := GetArticleForNoun(kind, " ")
            doc := "create" + article + kind
            if isSubresource {
                doc = "create " + subresource + " of" + article + kind
            }

            route := ws.POST(action.Path).To(handler).
                Doc(doc).
                Operation("create"+namespaced+kind+strings.Title(subresource)+operationSuffix).
                Produces(append(storageMeta.ProducesMIMETypes(action.Verb), mediaTypes...)...).
                Returns(http.StatusOK, "OK", producedObject).
                Returns(http.StatusCreated, "Created", producedObject).
                Returns(http.StatusAccepted, "Accepted", producedObject).
                Reads(defaultVersionedObject).
                Writes(producedObject)

            AddObjectParams(ws, route, versionedCreateOptions)
            addParams(route, action.Params)
            routes = append(routes, route)
        }

        for route := range routes {
            route.Metadata(ROUTE_META_GVK, metav1.GroupVersionKind{
                Group:   reqScope.Kind.Group,
                Version: reqScope.Kind.Version,
                Kind:    reqScope.Kind.Kind,
            })
            route.Metadata(ROUTE_META_ACTION, strings.ToLower(action.Verb))
            ws.Route(route)
        }
```

## 3.1 kube-apiserver 请求处理过程

从 apiserver 的请求处理函数开始：

```go
// staging/src/k8s.io/apiserver/pkg/server/handler.go

func (d director) ServeHTTP(w http.ResponseWriter, req *http.Request) {
    path := req.URL.Path

    // check to see if our webservices want to claim this path
    for _, ws := range d.goRestfulContainer.RegisteredWebServices() {
        switch {
        case ws.RootPath() == "/apis":
            if path == "/apis" || path == "/apis/" {
                return d.goRestfulContainer.Dispatch(w, req)
            }

        case strings.HasPrefix(path, ws.RootPath()):
            if len(path) == len(ws.RootPath()) || path[len(ws.RootPath())] == '/' {
                return d.goRestfulContainer.Dispatch(w, req)
            }
        }
    }

    // if we didn't find a match, then we just skip gorestful altogether
    d.nonGoRestfulMux.ServeHTTP(w, req)
}
```

如果能匹配到请求（例如匹配到前面注册的路由），它将
[分派给相应的 handler](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/server/handler.go#L136)
；否则，fall back 到
[path-based handler](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/server/mux/pathrecorder.go#L146)
（`GET /apis` 到达的就是这里）；

基于 path 的 handlers：

```go
// staging/src/k8s.io/apiserver/pkg/server/mux/pathrecorder.go

func (h *pathHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
    if exactHandler, ok := h.pathToHandler[r.URL.Path]; ok {
        return exactHandler.ServeHTTP(w, r)
    }

    for prefixHandler := range h.prefixHandlers {
        if strings.HasPrefix(r.URL.Path, prefixHandler.prefix) {
            return prefixHandler.handler.ServeHTTP(w, r)
        }
    }

    h.notFoundHandler.ServeHTTP(w, r)
}
```

如果还是没有找到路由，就会 fallback 到 non-gorestful handler，最终可能是一个 not found handler。

对于我们的场景，会匹配到一条已经注册的、名为
[`createHandler`](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/endpoints/handlers/create.go#L37)
为的路由。

## 3.2 Create handler 处理过程

```go
// staging/src/k8s.io/apiserver/pkg/endpoints/handlers/create.go

func createHandler(r rest.NamedCreater, scope *RequestScope, admit Interface, includeName bool) http.HandlerFunc {
    return func(w http.ResponseWriter, req *http.Request) {
        namespace, name := scope.Namer.Name(req) // 获取资源的 namespace 和 name（etcd item key）
        s := negotiation.NegotiateInputSerializer(req, false, scope.Serializer)

        body := limitedReadBody(req, scope.MaxRequestBodyBytes)
        obj, gvk := decoder.Decode(body, &defaultGVK, original)

        admit = admission.WithAudit(admit, ae)

        requestFunc := func() (runtime.Object, error) {
            return r.Create(
                name,
                obj,
                rest.AdmissionToValidateObjectFunc(admit, admissionAttributes, scope),
            )
        }

        result := finishRequest(ctx, func() (runtime.Object, error) {
            if scope.FieldManager != nil {
                liveObj := scope.Creater.New(scope.Kind)
                obj = scope.FieldManager.UpdateNoErrors(liveObj, obj, managerOrUserAgent(options.FieldManager, req.UserAgent()))
                admit = fieldmanager.NewManagedFieldsValidatingAdmissionController(admit)
            }

            admit.(admission.MutationInterface)
            mutatingAdmission.Handles(admission.Create)
            mutatingAdmission.Admit(ctx, admissionAttributes, scope)

            return requestFunc()
        })

        code := http.StatusCreated
        status, ok := result.(*metav1.Status)
        transformResponseObject(ctx, scope, trace, req, w, code, outputMediaType, result)
    }
}
```

1. 首先解析 HTTP request，然后执行基本的验证，例如保证 JSON 与 versioned API resource 期望的是一致的；
1. 执行审计和最终 admission；

    这里会执行所谓的 **<mark>Mutation</mark>** 操作，例如，如果 pod 打了 `sidecar-injector-webhook.xxx/inject: true` 标签，并且配置了合适的 Mutation webhook 和 server，
    在这一步就会给它**<mark>自动注入 sidecar</mark>**，完整例子可参考 IBM Cloud 博客
    [Diving into Kubernetes MutatingAdmissionWebhook](https://medium.com/ibm-cloud/diving-into-kubernetes-mutatingadmissionwebhook-6ef3c5695f74)。

    <p align="center"><img src="/assets/img/what-happens-when-k8s-creates-pods/mutating-admission-webhook.jpg" width="80%" height="80%"></p>
    <p align="center">Image Credit: <a href="https://medium.com/ibm-cloud/diving-into-kubernetes-mutatingadmissionwebhook-6ef3c5695f74">IBM Cloud</a></p>

1. 将资源最终[写到 etcd](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/endpoints/handlers/create.go#L401)，
   这会进一步调用到 [storage provider](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/registry/generic/registry/store.go#L362)。

   **<mark>etcd key 的格式一般是</mark>** `<namespace>/<name>`（例如，`default/nginx-0`），但这个也是可配置的。

1. 最后，storage provider 执行一次 `get` 操作，确保对象真的创建成功了。如果有额外的收尾任务（additional finalization），会执行
   post-create handlers 和 decorators。
1. 返回 [生成的](https://github.com/kubernetes/kubernetes/blob/v1.21.0/staging/src/k8s.io/apiserver/pkg/endpoints/handlers/create.go#L131-L142)
   HTTP response。

以上过程可以看出，apiserver 做了大量的事情。

总结：至此我们的 pod 资源已经在 etcd 中了。但是，此时 `kubectl get pods -n <ns>` 还看不见它。

# 4 Initializers

**<mark>对象持久化到 etcd 之后，apiserver 并未将其置位对外可见，它也不会立即就被调度</mark>**，
而是要先等一些 [initializers](https://kubernetes.io/docs/admin/extensible-admission-controllers/#initializers) 运行完成。

## 4.1 Initializer

Initializer 是**<mark>与特定资源类型（resource type）相关的 controller</mark>**，

* 负责**在该资源对外可见之前对它们执行一些处理**，
* 如果一种资源类型没有注册任何 initializer，这个步骤就会跳过，**资源对外立即可见**。

这是一种非常强大的特性，使得我们能**执行一些通用的启动初始化（bootstrap）操作**。例如，

- 向 Pod 注入 sidecar、暴露 80 端口，或打上特定的 annotation。
- 向某个 namespace 内的所有 pod 注入一个存放了测试证书（test certificates）的 volume。
- 禁止创建长度小于 20 个字符的 Secret （例如密码）。

## 4.2 InitializerConfiguration

可以用 `InitializerConfiguration` **<mark>声明对哪些资源类型（resource type）执行哪些 initializer</mark>**。

例如，要实现所有 pod 创建时都运行一个自定义的 initializer `custom-pod-initializer`，
可以用下面的 yaml：

```yaml
apiVersion: admissionregistration.k8s.io/v1alpha1
kind: InitializerConfiguration
metadata:
  name: custom-pod-initializer
initializers:
  - name: podimage.example.com
    rules:
      - apiGroups:
          - ""
        apiVersions:
          - v1
        resources:
          - pods
```

创建以上配置（`kubectl create -f xx.yaml`）之后，K8s 会将
`custom-pod-initializer` **<mark>追加到每个 pod 的 <code>metadata.initializers.pending</code> 字段</mark>**。

在此之前需要**<mark>启动 initializer controller</mark>**，它会

* 定期扫描是否有新 pod 创建；
* 当**检测到它的名字出现在 pod 的 pending 字段**时，就会执行它的处理逻辑；
* 执行完成之后，它会将自己的名字从 pending list 中移除。

pending list 中的 initializers，每次只有第一个 initializer 能执行。
当**所有 initializer 执行完成，`pending` 字段为空**之后，就认为
**<mark>这个对象已经完成初始化了</mark>**（considered initialized）。

细心的同学可能会有疑问：**前面说这个对象还没有对外可见，那用
户空间的 initializer controller 又是如何能检测并操作这个对象的呢？**答案是：
kube-apiserver 提供了一个 `?includeUninitialized` 查询参数，它会返回所有对象，
包括那些还未完成初始化的（uninitialized ones）。
