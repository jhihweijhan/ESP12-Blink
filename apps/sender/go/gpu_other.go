//go:build !windows

package main

// readGPUTempD3DKMT is a no-op on non-Windows platforms
func readGPUTempD3DKMT() (temp float64, ok bool) {
	return 0, false
}
