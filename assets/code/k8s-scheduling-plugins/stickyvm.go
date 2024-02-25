package stickyvm

import (
	"context"
	"fmt"

	v1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/client-go/rest"
	"k8s.io/client-go/util/retry"
	"k8s.io/klog/v2"
	"k8s.io/kubernetes/pkg/scheduler/framework"
)

const (
	// Name of the plugin used in the plugin registry and configurations.
	Name     = "StickyVM"
	stateKey = Name + "StateKey"

	kindVM  = "VirtualMachine"
	kindVMI = "VirtualMachineInstance"

	// Annotation key on VirtualMachine, value is the sticky node (if not empty)
	// Here we assume one VM has only one Pod.
	stickyAnnotationKey = "example.com/sticky-node"
)

var (
	_ framework.PreFilterPlugin = &StickyVM{}
	_ framework.FilterPlugin    = &StickyVM{}
	_ framework.PostBindPlugin  = &StickyVM{}
)

// StickyVM is a scheduling plugin for kubevirt VM pods
type StickyVM struct {
	kubevirtClient kubevirtv1.KubevirtV1Interface
}

type stickyState struct {
	nodeExists bool
	node       string
}

// Name returns name of the plugin
func (pl *StickyVM) Name() string {
	return Name
}

// New initializes a new plugin and returns it.
func New(rawArgs runtime.Object, h framework.Handle) (framework.Plugin, error) {
	useInClusterConfig := true
	klog.Infof("Initializing StickyVM scheduling plugin")

	var k8sConfig *rest.Config
	if useInClusterConfig {
		klog.Infof("Using in cluster configurations")
		config, err := rest.InClusterConfig()
		if err != nil {
			klog.Errorf("Get InClusterConfig failed: %v", err)
			return nil, fmt.Errorf("Get InClusterConfig failed: %v", err)
		}
		k8sConfig = config
	} else {
		klog.Infof("Using local development mode")
		k8sConfig = &rest.Config{
			Host: "<apiserver ip>:443",
			TLSClientConfig: rest.TLSClientConfig{
				CertFile: "./admin.crt",
				KeyFile:  "./admin.key",
				CAFile:   "./ca.crt",
			},
		}
	}

	klog.Infof("Creating kubevirt clientset")
	kubevirtClientSet, err := versioned.NewForConfig(k8sConfig)
	if err != nil {
		klog.Infof("Create kubevirt clientset failed: %v", err)
		return nil, fmt.Errorf("create kubevirt client failed: %v", err)
	}

	klog.Infof("Create kubevirt clientset successful")

	pl := StickyVM{
		kubevirtClient: kubevirtClientSet.KubevirtV1(),
	}

	klog.Infof("Initializing StickyVM scheduling plugin successful")
	return &pl, nil
}

// PreFilter invoked at the preFilter extension point.
func (pl *StickyVM) PreFilter(ctx context.Context, state *framework.CycleState, pod *v1.Pod) (*framework.PreFilterResult, *framework.Status) {
	klog.Infof("Prefilter %s/%s: start", pod.Namespace, pod.Name)
	s := stickyState{false, ""}
	defer func() {
		state.Write(stateKey, &s)
	}()

	// Get pod owner reference
	podOwnerRef := getPodOwnerRef(pod)
	if podOwnerRef == nil {
		klog.Infof("PreFilter: pod OwnerRef not found or doesn't meet expectation, skip sticky operations")
		return nil, framework.NewStatus(framework.Success, "Pod owner ref not found, return")
	}

	// Get VMI
	vmiName := podOwnerRef.Name
	ns := pod.Namespace
	klog.Infof("PreFilter: parent is %s %s", kindVMI, vmiName)

	vmi, err := pl.kubevirtClient.VirtualMachineInstances(ns).Get(context.TODO(), vmiName, metav1.GetOptions{ResourceVersion: "0"})
	if err != nil {
		klog.Infof("Get VMI %s/%s failed: %v", ns, vmiName, err)
		return nil, framework.NewStatus(framework.Error, "get vmi failed")
	}

	klog.Infof("PreFilter: found corresponding VMI")

	vmiOwnerRef := getVMIOwnerRef(vmi)
	if vmiOwnerRef == nil {
		klog.Infof("PreFilter: vmi OwnerRef not found or doesn't meet expectation, skip sticky operations")
		return nil, framework.NewStatus(framework.Success, "VMI owner ref not found, return")
	}

	// Get VM
	vmName := vmiOwnerRef.Name
	vm, err := pl.kubevirtClient.VirtualMachines(ns).Get(context.TODO(), vmName, metav1.GetOptions{ResourceVersion: "0"})
	if err != nil {
		klog.Infof("PreFilter: get VM %s/%s failed: %v", ns, vmiName, err)
		return nil, framework.NewStatus(framework.Error, "get vmi failed")
	}

	klog.Infof("PreFilter: found corresponding VM")

	// Annotate sticky node to VM
	s.node, s.nodeExists = vm.Annotations[stickyAnnotationKey]
	if s.nodeExists {
		klog.Infof("PreFilter: VM already sticky to node %s, write to scheduling context", s.node)
	} else {
		klog.Infof("PreFilter: VM has no sticky node, skip to write to scheduling context")
	}

	klog.Infof("Prefilter %s/%s: finish", pod.Namespace, pod.Name)
	return nil, framework.NewStatus(framework.Success, "Check pod/vmi/vm finish, return")
}

func (pl *StickyVM) Filter(ctx context.Context, state *framework.CycleState, pod *v1.Pod, nodeInfo *framework.NodeInfo) *framework.Status {
	klog.Infof("Filter %s/%s: start", pod.Namespace, pod.Name)
	s, err := state.Read(stateKey)
	if err != nil {
		klog.Infof("Filter: pod %s/%s: read preFilter scheduling context failed: %v", pod.Namespace, pod.Name, err)
		return framework.NewStatus(framework.Error, fmt.Sprintf("read preFilter state fail: %v", err))
	}

	r, ok := s.(*stickyState)
	if !ok {
		klog.Errorf("Filter: pod %s/%s: scheduling context found but sticky node missing", pod.Namespace, pod.Name)
		return framework.NewStatus(framework.Error, fmt.Sprintf("convert %+v to stickyState fail", s))
	}

	if !r.nodeExists {
		klog.Infof("Filter: pod %v/%v, sticky node not exist, got %s, return success", pod.Namespace, pod.Name, nodeInfo.Node().Name)
		return nil
	}

	if r.node != nodeInfo.Node().Name {
		// returning "framework.Error" will prevent process on other nodes
		klog.Infof("Filter: %v/%v, already stick to %s, skip %s", pod.Namespace, pod.Name, r.node, nodeInfo.Node().Name)
		return framework.NewStatus(framework.Unschedulable, "already stick to another node")
	}

	klog.Infof("Filter %s/%s: given node is the sticky node %s, use it", pod.Namespace, pod.Name, r.node)
	klog.Infof("Filter %s/%s: finish", pod.Namespace, pod.Name)
	return nil
}

func (pl *StickyVM) PostBind(ctx context.Context, state *framework.CycleState, pod *v1.Pod, nodeName string) {
	klog.Infof("PostBind %s/%s: start", pod.Namespace, pod.Name)

	s, err := state.Read(stateKey)
	if err != nil {
		klog.Infof("PostBind: pod %s/%s: read preFilter scheduling context failed: %v", pod.Namespace, pod.Name, err)
		return
	}

	r, ok := s.(*stickyState)
	if !ok {
		klog.Errorf("PostBind: pod %s/%s: convert failed", pod.Namespace, pod.Name)
		return
	}

	if r.nodeExists {
		klog.Errorf("PostBind: VM already has sticky annotation, return")
		return
	}

	klog.Infof("PostBind: annotating selected node %s to VM", nodeName)
	{
		// Get pod owner reference
		podOwnerRef := getPodOwnerRef(pod)
		if podOwnerRef == nil {
			klog.Infof("PostBind: pod OwnerRef not found or doesn't meet expectation, skip sticky operations")
			return
		}

		// Get VMI owner reference
		vmiName := podOwnerRef.Name
		ns := pod.Namespace
		klog.Infof("PostBind: parent is %s %s", kindVMI, vmiName)

		vmi, err := pl.kubevirtClient.VirtualMachineInstances(ns).Get(context.TODO(), vmiName, metav1.GetOptions{ResourceVersion: "0"})
		if err != nil {
			klog.Infof("PostBind: get VMI %s/%s failed: %v", ns, vmiName, err)
			return
		}

		klog.Infof("PostBind: found corresponding VMI")

		vmiOwnerRef := getVMIOwnerRef(vmi)
		if vmiOwnerRef == nil {
			klog.Infof("PostBind: vmi OwnerRef not found or doesn't meet expectation, skip sticky operations")
			return
		}

		// Add sticky node to VM annotations
		retry.RetryOnConflict(retry.DefaultRetry, func() error {
			vmName := vmiOwnerRef.Name
			vm, err := pl.kubevirtClient.VirtualMachines(ns).Get(context.TODO(), vmName, metav1.GetOptions{ResourceVersion: "0"})
			if err != nil {
				klog.Infof("PostBind: get VM %s/%s failed: %v", ns, vmiName, err)
				return err
			}

			klog.Infof("PostBind: found corresponding VM")

			if vm.Annotations == nil {
				vm.Annotations = make(map[string]string)
			}

			klog.Infof("PostBind: annotating node %s to VM: %v", nodeName, vm.Name)
			vm.Annotations[stickyAnnotationKey] = nodeName
			if _, err = pl.kubevirtClient.VirtualMachines(pod.Namespace).Update(ctx, vm, metav1.UpdateOptions{}); err != nil {
				klog.Infof("PostBind: update VM failed: %v", err)
				return err
			}
			return nil
		})
	}

	klog.Infof("PostBind %s/%s: finish", pod.Namespace, pod.Name)
}

// PreFilterExtensions returns prefilter extensions, pod add and remove.
func (pl *StickyVM) PreFilterExtensions() framework.PreFilterExtensions {
	return pl
}

// AddPod from pre-computed data in cycleState.
// no current need for this method.
func (pl *StickyVM) AddPod(ctx context.Context, cycleState *framework.CycleState, podToSchedule *v1.Pod, podToAdd *framework.PodInfo, nodeInfo *framework.NodeInfo) *framework.Status {
	return framework.NewStatus(framework.Success, "")
}

// RemovePod from pre-computed data in cycleState.
// no current need for this method.
func (pl *StickyVM) RemovePod(ctx context.Context, cycleState *framework.CycleState, podToSchedule *v1.Pod, podToRemove *framework.PodInfo, nodeInfo *framework.NodeInfo) *framework.Status {
	return framework.NewStatus(framework.Success, "")
}

// getPodOwnerRef returns the controller of the pod
func getPodOwnerRef(pod *v1.Pod) *metav1.OwnerReference {
	if len(pod.OwnerReferences) == 0 {
		return nil
	}

	for i := range pod.OwnerReferences {
		ref := &pod.OwnerReferences[i]
		if ref.Controller != nil && *ref.Controller && ref.Kind == kindVMI {
			return ref
		}
	}

	return nil
}

// getVMIOwnerRef returns the controller of the VMI
func getVMIOwnerRef(vmi *kubevirtapisv1.VirtualMachineInstance) *metav1.OwnerReference {
	if len(vmi.OwnerReferences) == 0 {
		return nil
	}

	for i := range vmi.OwnerReferences {
		ref := &vmi.OwnerReferences[i]
		if ref.Controller != nil && *ref.Controller && ref.Kind == kindVM {
			return ref
		}
	}

	return nil
}

// Clone don't really copy the data since there is no need
func (s *stickyState) Clone() framework.StateData {
	return s
}
