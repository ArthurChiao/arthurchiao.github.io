---
layout    : post
title     : "Cracking Kubernetes RBAC Authorization Model"
date      : 2022-04-17
lastupdate: 2022-05-10
categories: k8s authz rbac
canonical_url: https://learnk8s.io/rbac-kubernetes
---

This post first appeared as [Limiting access to Kubernetes resources with RBAC](https://learnk8s.io/rbac-kubernetes),
which was kindly edited, re-illustrated and exemplified by [learnk8s.io](https://learnk8s.io/), and
very friendly to beginners.

The version posted here in contrast has a biased focus on the design and
implementation, as well as in-depth discussions.

## TL; DR

This post digs into the Kubernetes RBAC authorization (AuthZ) model.

Specifically, given technical requirements of **<mark>granting proper
permissions to an application</mark>** to access `kube-apiserver`,
we'll introduce concepts like `User`, `ServiceAccount`, `Subject`,
`Resource`, `Verb`, `APIGroup`, `Rule`, `Role`, `RoleBinding` etc step by step, and
eventually build a RBAC authorization model by our own.

Hope that after reading this post, readers will have a deeper understanding on
the access control (AuthZ) of `kube-apiserver`.

----

* TOC
{:toc}

----

# 1 Introduction

## 1.1 Authentication (AuthN) and authorization (AuthZ)

These two spelling-alike but conceptially-different terms have confused
people for quite a while. In short,

* **<mark>AuthN</mark>** examines **<mark>who you are</mark>** (e.g. whether you're a valid user);
* **<mark>AuthZ</mark>** comes after AuthN, and checks **<mark>whether you have the permissions</mark>**
  to perform the actions you claimed (e.g. writing to a spcific database).

For example, Bob issues a `GET /api/databases?user=alice` request to a server,
intending to get the database list of Alice,

<p align="center"><img src="/assets/img/cracking-k8s-authz-rbac/authn-authz.png" width="80%" height="80%"></p>
<p align="center">Fig 1-1. AuthN and AuthZ when processing a client request</p>

then the server would perform following operations sequentially:

1. AuthN: on receving the request, authenticate if Bob is a valid user,

     1. On validation failure: reject the request by returning `401 Unauthorized`
       ([a long-standing misnomer](https://stackoverflow.com/a/6937030/4747193),
       **<mark>"401 Unauthenticated" would be more appropriate</mark>**);
     2. Otherwise (**<mark>authenticated</mark>**): enter the next stage (AuthZ).

2. AuthZ: check if Bob has the permission to list Alice's databases,

    1. If no permission granted, reject the request by returning `403 Forbidden`;
    2. Otherwise (**<mark>authorized</mark>**), go to the real processing logic.

3. Application processing logic: return `2xx` on success or `5xx` on internal server errors.

**<mark>This post will focus on authorization (AuthZ)</mark>**. Actually,
there are already many models designed for AuthZ, among them is RBAC
(Role-Based Access Control).

## 1.2 RBAC for AuthZ

RBAC is a method of regulating access to server-side resources based on
the roles of individual users within an organization.
The general idea is that instead of directly binding resource accessing
permissions to users with `(User,Permission,Resource)` as shown below,

<p align="center"><img src="/assets/img/cracking-k8s-authz-rbac/access-directly.png" width="60%" height="60%"></p>
<p align="center">Fig 1-2. Non role-based access control model: granting permissions directly to end users</p>

RBAC introduces the `Role` and `RoleBinding` concepts, described with 
<code>(User,RoleBinding,Role,Permission,Resource)</code>,
and facilitates security administration in large organizations with lots of
users permissions:

<p align="center"><img src="/assets/img/cracking-k8s-authz-rbac/access-with-role.png" width="60%" height="60%"></p>
<p align="center">Fig 1-3. Role based access control model</p>

RBAC is not a recent invention, but can date back to couples of years ago. In fact,
it is an approach for implementing mandatory access control (**<mark>MAC</mark>**)
and discretionary access control (**<mark>DAC</mark>**),
refer to [2,3] for more information.

## 1.3 RBAC in Kubernetes

K8s implements a RBAC model (as welll as several other models) for protecting
resources in the cluster. In more plain words, it manages the permissions to kube-apiserver's API.
With properly configured RBAC policies, we can control which user (human or programs) can access
which resource (pods/services/endpoints/...) with which operation (get/list/delete/...).

> Configure authorization mode for the cluster by passing
> **<mark><code>--authorization-mode=MODE_LIST</code></mark>** to `kube-apiserver`,
> where `MODE_LIST` is a comma separated list with the following valid values:
> **<mark><code>RBAC/ABAC/Node/AlwaysDeny/AlwaysAllow</code></mark>**.
>
> Refer to Kubernetes Documentation [Authorization Overview](https://kubernetes.io/docs/reference/access-authn-authz/authorization/) [1]
> for more information.

As an example, if you've played around for a while with Kubernetes, you'd have
seen things like this:

```yaml
apiVersion: v1
kind: ServiceAccount
metadata:
  name: serviceaccount:app1
  namespace: demo-namespace
----
apiVersion: rbac.authorization.k8s.io/v1
kind: Role
metadata:
  name: role:viewer
  namespace: demo-namespace
rules:        # Authorization rules for this role
- apiGroups:  # 1st API group
  - ""        # An empty string designates the core API group.
  resources:
  - services
  - endpoints
  - namespaces
  verbs:
  - get
  - list
  - watch
- apiGroups:  # 2nd API group
  - apiextensions.k8s.io
  resources:
  - customresourcedefinitions
  verbs:
  - get
  - list
  - watch
- apiGroups:  # 3rd API group
  - cilium.io
  resources:
  - ciliumnetworkpolicies
  - ciliumnetworkpolicies/status
  verbs:
  - get
  - list
  - watch
----
apiVersion: rbac.authorization.k8s.io/v1
kind: RoleBinding
metadata:
  name: rolebinding:app1-viewer
  namespace: demo-namespace
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: Role
  name: role:viewer
subjects:
- kind: ServiceAccount
  name: serviceaccount:app1
  namespace: demo-namespace
```

This is just a permission declaration in Kubernetes' RBAC language, after
applied, it will create:

1. An account for a service (program),
2. A role and the permission it has, and
3. A role-binding to bind the account to that role, so the program from that
   service could access the resources (e.g. list namespaces) of the
   `kube-apiserver`.

## 1.4 Purpose of this pose

This post will digs into the RBAC model in Kubernetes by **<mark>designing it by ourselves</mark>**.

Specifically, given the following requirements: granting read
(`get/list/watch`) permissions of the following APIs in `kube-apiserver` to an
application (`app1`) in the cluster:

```shell
# 1. Kubernetes builtin resources
/api/v1/namespaces/{namespace}/services
/api/v1/namespaces/{namespace}/endpoints
/api/v1/namespaces/{namespace}/namespaces

# 2. A specific API extention provided by cilium.io
/apis/cilium.io/v2/namespaces/{namespace}/ciliumnetworkpolicies
/apis/cilium.io/v2/namespaces/{namespace}/ciliumnetworkpolicies/status
```

Let's see how we could design a toy model to fulfill the requirements.

# 2 RBAC modeling

In Kubernetes, authorization is a server side privilege-checking procedure
against the incoming requests from an authenticated client.  In the remaining of
this section, we will split our modeling work into several parts:

1. The client side: client identification and representation;
2. The server side: resources (APIs) representation and organization;
3. Permission organization and management.

## 2.1 Client side modeling

Let's start from the simplest case, then gradually go through the complex ones.

### 2.1.1 Identify out-of-cluster users/programs: introducing `User`

The first case: suppose your new colleague would like to log in the Kubernetes
dashboard, or to use the CLI.  For this scenario, we should have a concept in the model to
denote what's **<mark>an "account" or a "user"</mark>**, with each of them having a unique name or ID
(such as email address), as shown below:

<p align="center"><img src="/assets/img/cracking-k8s-authz-rbac/rbac-user.png" width="60%" height="60%"></p>
<p align="center">Fig 2-1. Introdecing User: identify human users and other accounts outside of the cluster</p>

```go
type User struct {
    name string   // unique for each user
    ...           // other fields
}
```

Then we can **<mark>refer to a user</mark>** with something like the following in a YAML file:

```yaml
- kind: User
  name: "alice@example.com"
```

> Note that the `User` concept introduced here is used for
> **<mark>human or processes outside of the cluster</mark>** (as opposed to
> the `ServiceAccount` concept that will described in the next, which identifies
> accounts created and managed by Kubernetes itself). For example, the user
> account may come from the LDAP in your organization, so **<mark>AuthN of the user</mark>**
> may be done through something like OAuth2, TLS certificates, tokens;
> the subsequent **<mark>AuthZ</mark>** process will be the same as ServiceAccount clients.

### 2.1.2 Identify in-cluster program clients: introducing `ServiceAccount`

Most of the time, it is applications inside the Kubernetes cluster instead of human users
that are accessing the kube-apiserver, such as

* `cilium-agent` as a networking agent (deployed as daemonset) would like to list all pod resources on a specific node,
* `ingress-nginx-controller` as a L7 gateway would like to list all the backend endpoints of a specific service,

As those applications are created and managed by Kubernetes, we're responsible
for their identities. So we introduce ServiceAccount (SA),
a namespaced **<mark>account for an application in a Kubernetes cluster</mark>**,
just like a human account for an employee in a corp.

<p align="center"><img src="/assets/img/cracking-k8s-authz-rbac/rbac-sa.png" width="70%" height="70%"></p>
<p align="center">Fig 2-2. Introducing ServiceAccount: identify applications inside the Kubernetes cluster</p>

As SA is an application level account, all pods of an application share the SA,
thus have exactly the same permissions. SA is introduced and will be stored in
Kubernetes, so we can define a ServiceAccount with the following YAML specification:

```yaml
apiVersion: v1
kind: ServiceAccount
metadata:
  name: sa:app1             # arbitrary but unique string
  namespace: demo-namespace
```

> We can not **<mark>define</mark>** a `User` as they are managed by external systems outside of
> Kubernetes, such as LDAP or AD. Instead, we can only **<mark>refer to</mark>** `User`s.

Then refer it with:

```yaml
- kind: ServiceAccount
  name: sa:app1
  namespace: demo-namespace
```

### 2.1.3 Identify multiple clients: introducing `Group`

To facilitate Kubernetes administration,
we'd better also to support a group of `User`s or `ServiceAccount`s,

<p align="center"><img src="/assets/img/cracking-k8s-authz-rbac/rbac-subject-group.png" width="70%" height="70%"></p>
<p align="center">Fig 2-3. Introducing Group: a collection of Users or ServiceAccounts</p>

For example, with this capability, we could refer **<mark>all ServiceAccounts in a specific namespace</mark>**:

```yaml
- kind: Group
  name: system:serviceaccounts
  namespace: demo-namespace
```

### 2.1.4 General client referring: introducing `Subject`

With several client types being introduced, we're now ready to introduce a general
container for them: `Subject`. A subject list could contain different kinds of
client types, such as

```yaml
subjects:
- kind: User
  name: "alice@example.com"
- kind: ServiceAccount
  name: default
  namespace: kube-system
```

<p align="center"><img src="/assets/img/cracking-k8s-authz-rbac/rbac-subjects.png" width="70%" height="70%"></p>
<p align="center">Fig 2-4. Introducing Subject: a general representation of all kinds of clients and client groups</p>

This makes our policy more expressive and powerful.

With the client side identification accomplished, let's see the server side.

## 2.2 Server side modeling

The server side part is more complex, as this is where we will have to handle the authorization and authentication.

Again, we start from the smallest unit.

### 2.2.1 Identify APIs/URLs: introducing `Resource`

Objects like pods, endpoints, services in a Kubernetes cluster are exposed
via built-in APIs/URIs, such as,

```
/api/v1/namespaces/{namespace}/pods/{name}
/api/v1/namespaces/{namespace}/pods/{name}/log
/api/v1/namespaces/{namespace}/serviceaccounts/{name}
```

These URIs are too long to be concisely described in an AuthZ policy specification,
so we **<mark>introduce a short representation</mark>**: `Resource`. With
`Resource` denotation, the above APIs can be referred by:

```yaml
  resources:
  - pods
  - pods/log
  - serviceaccounts
```

### 2.2.2 Identify operations on `Resource`: introducing `Verb`

To describe the **<mark>permitted operations</mark>** on a given `Resource`,
e.g. whether read-only (`get/list/watch`) or write-update-delete
(`create/patch/delete`), we introduce a `Verb` concept:

```yaml
  resources:
  - ciliumnetworkpolicies
  - ciliumnetworkpolicies/status
  verbs:
  - get
  - list
  - watch
```

<p align="center"><img src="/assets/img/cracking-k8s-authz-rbac/rbac-verb-resource.png" width="45%" height="45%"></p>
<p align="center">Fig 2-5. Introducing Verb and Resource: express specific actions on specific APIs</p>

### 2.2.3 Distinguish `Resource`s from different API providers: introducing `APIGroup`

One thing we have intentionally skipped discussing during the `Resource` section:
apart from APIs for built-in objects (pods, endpoints, services, etc), Kubernetes
also supports **<mark>API extensions</mark>**. Such as, if using Cilium as networking solution,
it will create "ciliumendpoint" custom resources (CRs) in the cluster,

```yaml
apiVersion: apiextensions.k8s.io/v1
kind: CustomResourceDefinition
metadata:
  name: ciliumendpoints.cilium.io
spec:
  group: cilium.io
  names:
    kind: CiliumEndpoint
  scope: Namespaced
  ...
```

Check the related objects already in the cluster:

```shell
$ k get ciliumendpoints.cilium.io -n demo-namespace
NAME   ENDPOINT ID   IDENTITY ID   INGRESS ENFORCE  EGRESS ENFORCE VISIBILITY POLICY   ENDPOINT STATE   IPV4
app1   2773          1628124                                                           ready            10.6.7.54
app2   3568          1624494                                                           ready            10.6.7.94
app3   3934          1575701                                                           ready            10.6.4.24
```

These custom resource objects **<mark>can be accessed in a similar URI format as the built-int objects</mark>**,

```
/apis/cilium.io/v2/namespaces/{namespace}/ciliumendpoints
/apis/cilium.io/v2/namespaces/{namespace}/ciliumendpoints/{name}
```

So, **<mark>to make our short format resource denotation more general</mark>**,
we need to support this scenario, too. Enter `APIGroup`.
As the name tells, `APIGroup` split APIs (resources) into groups. In our design,
we just put resources and related verbs into an `apiGroups` section:

```yaml
- apiGroups:
  - cilium.io     # APIGroup name
  resources:
  - ciliumnetworkpolicies
  - ciliumnetworkpolicies/status
  verbs:
  - get
  - list
  - watch
```

**<mark>Depending on the "name" of an APIGroup</mark>**,

* If it is empty `""`, we expand the resources to `/api/v1/xxx`;
* Otherwise, we expand the resources to `/apis/{apigroup_name}/{apigroup_version}/xxx`;

as illustrated below:

<p align="center"><img src="/assets/img/cracking-k8s-authz-rbac/rbac-apigroup.png" width="70%" height="70%"></p>
<p align="center">Fig 2-6. Introducing APIGroup, and how APIGroup+Resource are expanded in the behind</p>

## 2.3 Combine `Verb`, `APIGroup` and `Resource`: introducing `Rule`

With `APIGroups` as the last piece introduced, we can **<mark>finally describe what's an AuthZ <code>Rule</code></mark>**:
actually it's **<mark>nothing more than an apiGroup list</mark>** that are allowed to be accessed, 

```yaml
rules:        # Authorization rules
- apiGroups:  # 1st API group
  - ""        # An empty string designates the core API group.
  resources:
  - services
  - endpoints
  - namespaces
  verbs:
  - get
  - list
  - watch
- apiGroups:  # another API group
  - cilium.io # Custom APIGroup
  resources:
  - ciliumnetworkpolicies
  - ciliumnetworkpolicies/status
  verbs:
  - get
  - list
  - watch
```

as illustrated below:

<p align="center"><img src="/assets/img/cracking-k8s-authz-rbac/rbac-rule.png" width="75%" height="75%"></p>
<p align="center">Fig 2-7. Introducing Rule: a list of allowed APIGroups</p>

## 2.4 Who has the permissions described by a `Rule`: introducing `Role`

With Rules and Subjects ready, we can assign Rules to Subjects, then clients
described subjects will have the permissions to access the resources described in the rules.
But as has been said, RBAC characterizes itself by `Roles`, which
**<mark>decouples individual users from individual rules</mark>**, and makes
privilege sharing and granting more powerful and managable.

So, instead of directly inserting rules into a Subject (Account/ServiceAccount),
we insert it into a Role:

```yaml
apiVersion: rbac.authorization.k8s.io/v1
kind: Role
metadata:
  name: viewer
rules:
- apiGroups:  # 1st API group
  - ""        # An empty string designates the core API group.
  resources:
  - pods
  verbs:
  - get
  - list
  - watch
  - delete
- apiGroups:
  ...
```

<p align="center"><img src="/assets/img/cracking-k8s-authz-rbac/rbac-role.png" width="75%" height="75%"></p>
<p align="center">Fig 2-8. Introducing Role: a list of allowed rules</p>

Note that we introduce `Role` as a kind of resource in Kubernetes, so here we have an
`apiVersion` field in the yaml, while `Resource`, `APIGroup`, `Verb` are just
internal concepts (and data structures).

## 2.5 Grant permissions to target clients: introducing `RoleBinding`

Now we have `Subject` to referring to all kinds of clients, `Rule` for describing allowed resources,
and also `Role` to describe who different kinds of rules, the last thing for us
is to **<mark>bind target clients to specific roles</mark>**. Enter `RoleBinding`.

A role binding grants the permissions described in a `Role` to one or multiple clients.
It holds a list of `Subject`s (users, groups, or service accounts), and a
reference to the role being granted.

For example, to bind the `viewer` role
to Subject `kind=ServiceAccount,name=sa-for-app1,namespace=demo-namespace`,

```yaml
apiVersion: rbac.authorization.k8s.io/v1
kind: RoleBinding
metadata:
  name: role-binding-for-app1               # RoleBinding name
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: Role
  name: viewer                              # Role to be referred
subjects:                                   # Clients that will be binded to the above Role
- kind: ServiceAccount
  name: sa-for-app1
  namespace: kube-system
```

Note that `RoleBinding` is also introduced as a kind of Kubernetes resource, so it has `apiVersion` field.

## 2.6 Example: an end-to-end workflow

**<mark>Now our RBAC modeling finished</mark>**, let's see an end-to-end workflow:
how to setup Kubernetes RBAC stuffs to
**<mark>allow an application to list <code>ciliumnetworkpolicies</code> resources</mark>** in the cluster.

<p align="center"><img src="/assets/img/cracking-k8s-authz-rbac/workflow.png" width="90%" height="90%"></p>
<p align="center">Fig 2-9. </p>

1. Cluster administrator: **<mark>create a ServiceAccount</mark>** for application `app1`;
2. Cluster administrator: **<mark>create a Role</mark>** to describe
   permissions allowed on specific resources;
3. Cluster administrator: **<mark>create a RoleBinding</mark>** to bind the service account to the role;
4. Client: **<mark>send a request to kube-apiserver</mark>**, e.g. to list all
   the `ciliumnetworkpolicies` in a specific namespace;
5. `kube-apiserver` **<mark>AuthN: validate user</mark>**; on authenticated, **<mark>associate service account to Role</mark>**, go to 6;
6. `kube-apiserver` **<mark>AuthZ: check permissions (described in Role->Rule->APIGroups)</mark>**; on authorized, go to 7;
7. `kube-apiserver`: process request, retrieve all `ciliumnetworkpolicies` in the given namespace;
8. `kube-apiserver`: return results to the client.

Note that if AuthN or AuthZ failed, kube-apiserver will return directly with a proper error message.
Besides, we've also drawn a human user in the subject list when perform RoleBinding,
which is the case when the client is an out-of-cluster user or program.

## 2.7 Summary

With no surprises, the RBAC model we've designed is a simplified
version of the one shipped with Kubernetes (**<mark><code>rbac.authorization.k8s.io</code></mark>** API).
Hope that through this bottom-up
approach, you've had a better understanding of the RBAC model and related concepts.

In the next section, we'll have a quick glimpse of the RBAC implementations in Kubernetes.

# 3 Implemention

## 3.1 Data structures

Without make this post too long, we just point to some of the key data structures:

```go
// https://github.com/kubernetes/kubernetes/blob/v1.23.4/pkg/apis/rbac/types.go

// PolicyRule holds information that describes a policy rule, but does not contain information
// about who the rule applies to or which namespace the rule applies to.
type PolicyRule struct {
    Verbs     []string
    APIGroups []string
    Resources []string

    // Following types not covered in this post
    ResourceNames   []string
    NonResourceURLs []string
}

// Subject contains a reference to the object or user identities a role binding applies to.  This can either hold a direct API object reference,
// or a value for non-objects such as user and group names.
type Subject struct {
    Kind string
    APIGroup string
    Namespace string
}

// RoleRef contains information that points to the role being used
type RoleRef struct {
    APIGroup string // APIGroup is the group for the resource being referenced
    Kind string // Kind is the type of resource being referenced
    Name string // Name is the name of resource being referenced
}


// Role is a namespaced, logical grouping of PolicyRules that can be referenced as a unit by a RoleBinding.
type Role struct {
    Rules []PolicyRule
}

// RoleBinding references a role, but does not contain it.  It can reference a Role in the same namespace or a ClusterRole in the global namespace.
// It adds who information via Subjects and namespace information by which namespace it exists in.  RoleBindings in a given
// namespace only have effect in that namespace.
type RoleBinding struct {
    Subjects []Subject
    RoleRef RoleRef
}
```

## 3.2 Bootstrap policies

Some builtin roles, rules, etc:

```go
// https://github.com/kubernetes/kubernetes/blob/v1.23.4/plugin/pkg/auth/authorizer/rbac/bootstrappolicy/policy.go
```

# 4 Discussions

## 4.1 Namespaceless `Role`/`RoleBinding`: `ClusterRole`/`ClusterRoleBinding`

A `Role` sets permissions within a particular namespace. If you'd like to set
namespaceless roles, you can use `ClusterRole` (and the corresponding
`ClusterRoleBinding`).

> In perticular, **<mark>some Kubernetes resources are not namespace-scoped, such as Persistent Volumes and Nodes</mark>**.
>
> ```
> /api/v1/nodes/{name}
> /api/v1/persistentvolume/{name}
> ```

| Namespaced   | Namespaceless (Cluster wide) |
|:-------------|:-------------|
| Role         | ClusterRole  |
| RoleBinding  | ClusterRoleBinding  |

`kube-apiserver` creates **<mark>a set of default ClusterRole/ClusterRoleBinding</mark>** objects,

* many of these are **<mark><code>system:</code></mark>** prefixed, which
  indicates that the resource is directly managed by the cluster control plane;
* all of the default ClusterRoles and ClusterRoleBindings are labeled with
  **<mark><code>kubernetes.io/bootstrapping=rbac-defaults</code></mark>**.

## 4.2 Default roles and clusterroles in Kubernetes

Default roles:

```shell
$ k get roles -n kube-system | grep "^system:"
NAME                                             CREATED AT
system::leader-locking-kube-controller-manager   2021-05-10T08:52:46Z
system::leader-locking-kube-scheduler            2021-05-10T08:52:46Z
system:controller:bootstrap-signer               2021-05-10T08:52:45Z
system:controller:cloud-provider                 2021-05-10T08:52:45Z
system:controller:token-cleaner                  2021-05-10T08:52:45Z
...
```

Default clusterroles:

```shell
$ kubectl get clusterroles -n kube-system | grep "^system:"
system:aggregate-to-admin                               2021-05-10T08:52:44Z
system:aggregate-to-edit                                2021-05-10T08:52:44Z
system:aggregate-to-view                                2021-05-10T08:52:44Z
system:discovery                                        2021-05-10T08:52:44Z
system:kube-apiserver                                   2021-05-10T08:57:10Z
system:kube-controller-manager                          2021-05-10T08:52:44Z
system:kube-dns                                         2021-05-10T08:52:44Z
system:kube-scheduler                                   2021-05-10T08:52:44Z
...
```

To see the detailed permissions in each role/clusterrole,

```shell
$ kubectl get role <role> -n kube-system -o yaml
```

## 4.3 Embed service account info into pods

Service accounts are usually created automatically by the API server and
associated with pods running in the cluster. Three separate components
cooperate to implement the automation:

1. A **<mark>ServiceAccount admission controller</mark>**,
  [implementation](https://github.com/kubernetes/kubernetes/blob/v1.23.1/plugin/pkg/admission/serviceaccount/admission.go)

    Admission Control modules can modify or reject requests. In addition to the
    attributes available to Authorization modules, they can access the contents
    of the object that is being created or modified, e.g. injecting access
    token to pods.

1. A **<mark>Token controller</mark>**
1. A **<mark>ServiceAccount controller</mark>**

Service account bearer tokens are perfectly valid to use outside the cluster
and can be used to create identities for long standing jobs that wish to talk
to the Kubernetes API. To manually create a service account,

```shell
$ kubectl create serviceaccount demo-sa
serviceaccount/demo-sa created

$ k get serviceaccounts demo-sa -o yaml
apiVersion: v1
kind: ServiceAccount
metadata:
  name: demo-sa
  namespace: default
  resourceVersion: "1985126654"
  selfLink: /api/v1/namespaces/default/serviceaccounts/demo-sa
  uid: 01b2a3f9-a373-6e74-b3ae-d89f6c0e321f
secrets:
- name: demo-sa-token-hrfq2
```

The created secret holds the public CA of the API server and a signed JSON Web Token (JWT).

```shell
$ kubectl get secret demo-sa-token-hrfq2 -o yaml
apiVersion: v1
data:
  ca.crt: (APISERVER CA BASE64 ENCODED)
  namespace: ZGVmYXVsdA==
  token: (BEARER TOKEN BASE64 ENCODED)
kind: Secret
metadata:
  # ...
type: kubernetes.io/service-account-token
```

The signed JWT can be used as a bearer token to authenticate as the given
service account, and is included in request headers. Normally
these **<mark>secrets are mounted into pods</mark>** for in-cluster access to the API server,
but can be used from outside the cluster as well.

Service accounts authenticate with the username
**<mark><code>system:serviceaccount:NAMESPACE:SERVICEACCOUNT</code></mark>**,
and are assigned to the groups **<mark><code>system:serviceaccounts</code></mark>**
and **<mark><code>system:serviceaccounts:NAMESPACE</code></mark>**.

## 4.4 Internals of AuthZ process

More information on the authorization process, refer to Kubernetes documentation
[<mark>Authorization Overview</mark>](https://kubernetes.io/docs/reference/access-authn-authz/authorization/#review-your-request-attributes).

## 4.5 AuthN: rationals of differentiating `User` and `ServiceAccount`

A Kubernetes cluster has two categories of users [4]:

1. Service accounts: **<mark>managed by Kubernetes</mark>**,
2. Normal users: usually **<mark>managed by a cluster-independent service</mark>** in the following ways:

    * an administrator distributing private keys
    * a user store like Keystone or Google Accounts
    * a file with a list of usernames and passwords

### 4.5.1 Normal users

In this regard, **<mark>Kubernetes does not have objects which represent normal user accounts</mark>**.
Normal users cannot be added to a cluster through an API call.
But, **<mark>any user that presents a valid credential</mark>** is considered authenticated.

For more details, refer to the normal users topic in 
[certificate request](https://kubernetes.io/docs/reference/access-authn-authz/certificate-signing-requests/#normal-user)
for more details about this.

### 4.5.2 ServiceAccount users

In contrast, **<mark>service accounts are users managed by the Kubernetes API</mark>**.
They are,

* Created automatically by the API server or manually through API calls.
* Tied to a set of credentials stored as `Secrets`, which are mounted into pods
  allowing in-cluster processes to talk to the Kubernetes API.

More rationals behind the scene, see Kubernetes documentation
[User accounts versus service accounts](https://kubernetes.io/docs/reference/access-authn-authz/service-accounts-admin/).

## 4.6 Related configurations of kube-apiserver

```shell
$ cat /etc/kubernetes/apiserver.config
 --authorization-mode=Node,RBAC \
 --kubelet-certificate-authority=/etc/kubernetes/pki/ca.crt \
 --service-account-key-file=/etc/kubernetes/pki/sa.pub        # bearer tokens. If unspecified, the API server's TLS private key will be used.
 ...
```

## 4.7 Make AuthZ YAML policies more concise

A normal specification:

```yaml
rules:
- apiGroups:
  - ""
  resources:
  - pods
  - endpoints
  - namespaces
  verbs:
  - get
  - watch
  - list
  - create
  - delete
```

The above specification can be re-write in the following format:

```yaml
- apiGroups: [""]
  resources: ["services", "endpoints", "namespaces"]
  verbs:     ["get", "list", "watch", "create", "delete"]
```

which reduces lines significantly, and is more concise.
But Kubernetes internally still manages the content with long-format:

```shell
$ kubectl get role pod-reader -o yaml
apiVersion: rbac.authorization.k8s.io/v1
kind: Role
rules:
- apiGroups:
  - ""
  resources:
  - pods
...
```

## 4.8 Some subject examples

```yaml
subjects:
- kind: Group
  name: system:serviceaccounts:qa
  apiGroup: rbac.authorization.k8s.io

subjects:
- kind: Group
  name: system:serviceaccounts         # when namespace field is no specified: all service accounts in any namespace
  apiGroup: rbac.authorization.k8s.io

subjects:
- kind: Group
  name: system:authenticated           # for all authenticated users
  apiGroup: rbac.authorization.k8s.io
- kind: Group
  name: system:unauthenticated         # for all unauthenticated users
  apiGroup: rbac.authorization.k8s.io
```

## 4.9 Virtual `resource` types

From the [documentation](https://kubernetes.io/docs/reference/using-api/api-concepts/#standard-api-terminology),
in Kubernetes,

* Most resource types are
  [objects](https://kubernetes.io/docs/concepts/overview/working-with-objects/kubernetes-objects/#kubernetes-objects):
  they represent a **<mark>concrete instance of a concept on the cluster</mark>**, such as

  1. a `pod`,
  1. a `namespace`.

* A smaller number of API resource types are **<mark>virtual</mark>**, in that
  they often **<mark>represent operations on objects</mark>**, rather than
  objects themsellves, such as

  1. **<mark>a permission check</mark>** (use a POST with a JSON-encoded body of `SubjectAccessReview` to the `subjectaccessreviews` resource),
  1. the `eviction` sub-resource of a Pod (used to trigger API-initiated eviction).

# References

1. [Kubernetes Doc: Authorization Overview](https://kubernetes.io/docs/reference/access-authn-authz/authorization/)
2. [Wikipedia: RBAC](https://en.wikipedia.org/wiki/Role-based_access_control)
3. [RBAC as it meant to be](https://tailscale.com/blog/rbac-like-it-was-meant-to-be/)
4. [Kubernetes Doc: Authentication Overview](https://kubernetes.io/docs/reference/access-authn-authz/authentication/)
