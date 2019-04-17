---
layout: post
title:  "Ctrip Network Architecture Evolution in the Cloud Computing Era"
date:   2019-04-17
categories: network architecture datacenter
---

### Preface


----

## 0 Ctrip Cloud

### About Ctrip Cloud

Ctrip cloud team started at ~2013.

We started by providing virtual resources to our internal customers
based on OpenStack. Since then, we have developed our own baremetal platform,
and deployed container platforms like Mesos and K8S. After those years, we have
packed all our cloud services into a unified platform, we call it CDOS - Ctrip
Datacenter Operating System.

CDOS manages all our compute, network and storage resources on both private
cloud and public cloud (vendors). In the private cloud, we provide VM, BM and
container instances to our internal customers. In the public cloud, we have
integrated AWS, Tecent cloud, UCloud, etc vendors, and providing VM and
container instances to our internal customers.

### Network Architecture Timeline

## 1 VLAN-based L2 Network

## 2 SDN-based Large L2 Network

## 3 K8S & Hybrid Cloud Network

## 4 Cloud Native Solutions

## image

<p align="center"><img src="/assets/img/system-call-definitive-guide/idt.png" width="60%" height="60%"></p>
<p align="center">图 1 标题</p>

## internal ref

1. [(译) Linux系统调用权威指南]({% link _posts/2019-01-30-system-call-definitive-guide-zh.md %})
