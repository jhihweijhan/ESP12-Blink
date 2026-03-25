//go:build windows

package main

import (
	"fmt"
	"syscall"
	"unsafe"
)

var (
	gdi32                = syscall.NewLazyDLL("gdi32.dll")
	procEnumAdapters2    = gdi32.NewProc("D3DKMTEnumAdapters2")
	procQueryAdapterInfo = gdi32.NewProc("D3DKMTQueryAdapterInfo")
	procCloseAdapter     = gdi32.NewProc("D3DKMTCloseAdapter")
)

const (
	kmtqaiAdapterPerfData = 62
	maxAdapters           = 16
)

type d3dkmtEnumAdapters2 struct {
	NumAdapters uint32
	Adapters    *d3dkmtAdapterInfo
}

type d3dkmtAdapterInfo struct {
	Handle     uint32
	Luid       [8]byte
	NumSources uint32
	Present    uint32
}

type d3dkmtQueryAdapterInfo struct {
	Handle        uint32
	Type          uint32
	PrivateData   uintptr
	PrivateLength uint32
}

type d3dkmtCloseAdapter struct {
	Handle uint32
}

type d3dkmtAdapterPerfData struct {
	PhysicalAdapterIndex uint32
	MemoryFrequency      uint64
	MaxMemoryFrequency   uint64
	MaxMemoryBandwidth   uint64
	MemoryBandwidth      uint64
	PerGPUBandwidth      uint64
	GPUFrequency         uint64
	MaxGPUFrequency      uint64
	MaxGPUFrequencyOC    uint64
	Temperature          uint32
	FanRPM               uint32
	Power                uint32
	VoltageMax           uint32
	VoltageGfx           uint32
	VoltageSOC           uint32
	VoltageMem           uint32
}

// Also try KMTQAITYPE_QUERYADAPTERTYPE to get adapter description
const kmtqaiAdapterType = 15

type d3dkmtAdapterType struct {
	RenderSupported         uint32 // bitfield
	DisplaySupported        uint32
	SoftwareDevice          uint32
	PostDevice              uint32
	HybridDiscrete          uint32
	HybridIntegrated        uint32
	IndirectDisplayDevice   uint32
	Paravirtualized         uint32
	ACGSupported            uint32
	SupportSetTimingsFromVidPn uint32
	Detachable              uint32
	ComputeOnly             uint32
	Prototype               uint32
}

func testD3DKMT() {
	if err := procEnumAdapters2.Find(); err != nil {
		fmt.Printf("  D3DKMTEnumAdapters2 not found: %v\n", err)
		return
	}

	adapters := make([]d3dkmtAdapterInfo, maxAdapters)
	enumArgs := d3dkmtEnumAdapters2{
		NumAdapters: maxAdapters,
		Adapters:    &adapters[0],
	}

	r1, _, err := procEnumAdapters2.Call(uintptr(unsafe.Pointer(&enumArgs)))
	fmt.Printf("  EnumAdapters2 result: 0x%X (adapters: %d) err: %v\n", r1, enumArgs.NumAdapters, err)

	if r1 != 0 {
		fmt.Println("  EnumAdapters2 failed, trying fallback...")
		return
	}

	for i := uint32(0); i < enumArgs.NumAdapters; i++ {
		adapter := adapters[i]
		fmt.Printf("\n  Adapter %d: handle=0x%X sources=%d present=%d\n",
			i, adapter.Handle, adapter.NumSources, adapter.Present)

		// Query PERFDATA
		var perfData d3dkmtAdapterPerfData
		queryArgs := d3dkmtQueryAdapterInfo{
			Handle:        adapter.Handle,
			Type:          kmtqaiAdapterPerfData,
			PrivateData:   uintptr(unsafe.Pointer(&perfData)),
			PrivateLength: uint32(unsafe.Sizeof(perfData)),
		}

		r1, _, qErr := procQueryAdapterInfo.Call(uintptr(unsafe.Pointer(&queryArgs)))
		fmt.Printf("    PERFDATA query: 0x%X err: %v\n", r1, qErr)

		if r1 == 0 {
			fmt.Printf("    Temperature:   %d (deci-C) = %.1f°C\n", perfData.Temperature, float64(perfData.Temperature)/10.0)
			fmt.Printf("    FanRPM:        %d\n", perfData.FanRPM)
			fmt.Printf("    Power:         %d (deci-%%)\n", perfData.Power)
			fmt.Printf("    GPU Freq:      %d / %d MHz\n", perfData.GPUFrequency, perfData.MaxGPUFrequency)
			fmt.Printf("    Mem Freq:      %d / %d MHz\n", perfData.MemoryFrequency, perfData.MaxMemoryFrequency)
		} else {
			fmt.Printf("    PERFDATA not supported by this adapter's driver\n")
		}

		// Close adapter
		closeArgs := d3dkmtCloseAdapter{Handle: adapter.Handle}
		procCloseAdapter.Call(uintptr(unsafe.Pointer(&closeArgs)))
	}
}
