// GPU diagnostics tool — run on Windows to check what temperature APIs are available
//
// Build: GOOS=windows GOARCH=amd64 go build -o gpu_diag.exe ./cmd/gpu_diag/
// Run on Windows: .\gpu_diag.exe

package main

import (
	"fmt"
	"os/exec"
	"runtime"
	"strings"

	"github.com/shirou/gopsutil/v4/sensors"
)

func main() {
	fmt.Printf("=== GPU Temperature Diagnostics ===\n")
	fmt.Printf("OS: %s/%s\n\n", runtime.GOOS, runtime.GOARCH)

	// 1. Check nvidia-smi
	fmt.Println("--- nvidia-smi ---")
	nvidiaCmd := exec.Command("nvidia-smi",
		"--query-gpu=name,utilization.gpu,temperature.gpu,memory.used,memory.total",
		"--format=csv,noheader,nounits")
	if out, err := nvidiaCmd.Output(); err == nil {
		fmt.Printf("  Found: %s\n", strings.TrimSpace(string(out)))
	} else {
		fmt.Printf("  Not available: %v\n", err)
	}

	// 2. Check gopsutil sensors (WMI MSAcpi_ThermalZoneTemperature)
	fmt.Println("\n--- gopsutil sensors (WMI) ---")
	temps, err := sensors.SensorsTemperatures()
	if err != nil {
		fmt.Printf("  Error: %v\n", err)
	} else if len(temps) == 0 {
		fmt.Println("  No sensors found")
	} else {
		for _, t := range temps {
			fmt.Printf("  %-40s %.1f°C\n", t.SensorKey, t.Temperature)
		}
	}

	// 3. D3DKMT (Windows only — handled in gpu_diag_windows.go)
	fmt.Println("\n--- D3DKMT API ---")
	testD3DKMT()

	fmt.Println("\n=== Done ===")
	if runtime.GOOS == "windows" {
		fmt.Println("Press Enter to exit...")
		fmt.Scanln()
	}
}
