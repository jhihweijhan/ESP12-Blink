//go:build windows

package main

import (
	"fmt"
	"math"
	"syscall"
	"unsafe"
)

// D3DKMT API structures for GPU temperature reading on Windows
// Uses gdi32.dll (built-in, no extra install needed)
// Supports NVIDIA, AMD, and Intel GPUs via WDDM driver standard
// Requires Windows 10 1803+ (WDDM 2.4+)

var (
	gdi32                    = syscall.NewLazyDLL("gdi32.dll")
	procEnumAdapters2        = gdi32.NewProc("D3DKMTEnumAdapters2")
	procQueryAdapterInfo     = gdi32.NewProc("D3DKMTQueryAdapterInfo")
	procCloseAdapter         = gdi32.NewProc("D3DKMTCloseAdapter")
)

const (
	kmtqaiAdapterPerfData = 62 // KMTQAITYPE_ADAPTERPERFDATA
	maxAdapters           = 16
	statusSuccess         = 0
)

type d3dkmtEnumAdapters2 struct {
	NumAdapters uint32
	Adapters    *d3dkmtAdapterInfo
}

type d3dkmtAdapterInfo struct {
	Handle       uint32
	Luid         [8]byte // LUID
	NumSources   uint32
	PresentPath  uint32 // BOOL
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

// D3DKMT_ADAPTER_PERFDATA - performance data including temperature
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
	Temperature          uint32 // deci-Celsius (1 = 0.1°C)
	FanRPM               uint32
	Power                uint32 // deci-percent
	VoltageMax           uint32
	VoltageGfx           uint32
	VoltageSOC           uint32
	VoltageMem           uint32
}

// readGPUTempD3DKMT reads GPU temperature using Windows D3DKMT API
// Returns temperature in Celsius, 0 if unavailable
func readGPUTempD3DKMT() (temp float64, ok bool) {
	// Check if D3DKMT API is available
	if err := procEnumAdapters2.Find(); err != nil {
		return 0, false
	}
	if err := procQueryAdapterInfo.Find(); err != nil {
		return 0, false
	}

	adapters := make([]d3dkmtAdapterInfo, maxAdapters)
	enumArgs := d3dkmtEnumAdapters2{
		NumAdapters: maxAdapters,
		Adapters:    &adapters[0],
	}

	r1, _, _ := procEnumAdapters2.Call(uintptr(unsafe.Pointer(&enumArgs)))
	if r1 != statusSuccess {
		return 0, false
	}

	var bestTemp float64
	found := false

	for i := uint32(0); i < enumArgs.NumAdapters; i++ {
		adapter := adapters[i]

		var perfData d3dkmtAdapterPerfData
		queryArgs := d3dkmtQueryAdapterInfo{
			Handle:        adapter.Handle,
			Type:          kmtqaiAdapterPerfData,
			PrivateData:   uintptr(unsafe.Pointer(&perfData)),
			PrivateLength: uint32(unsafe.Sizeof(perfData)),
		}

		r1, _, _ := procQueryAdapterInfo.Call(uintptr(unsafe.Pointer(&queryArgs)))

		// Close adapter handle regardless of query result
		closeArgs := d3dkmtCloseAdapter{Handle: adapter.Handle}
		procCloseAdapter.Call(uintptr(unsafe.Pointer(&closeArgs)))

		if r1 != statusSuccess {
			continue
		}

		if perfData.Temperature > 0 && perfData.Temperature < 2000 { // < 200°C sanity
			tempC := float64(perfData.Temperature) / 10.0
			tempC = math.Round(tempC*10) / 10
			if tempC > bestTemp {
				bestTemp = tempC
				found = true
			}
		}
	}

	if found {
		fmt.Printf("D3DKMT GPU temp: %.1f°C\n", bestTemp)
	}

	return bestTemp, found
}
