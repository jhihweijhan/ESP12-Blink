//go:build !windows

package main

import "fmt"

func testD3DKMT() {
	fmt.Println("  D3DKMT API is Windows-only, skipped")
}
