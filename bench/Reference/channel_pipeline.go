package main

import "fmt"

const N = 5000

func main() {
	ch := make(chan int, 32)
	go func() {
		for n := 1; n <= N; n++ {
			ch <- n * 2
		}
		close(ch)
	}()
	sum := 0
	for v := range ch {
		sum += v
	}
	fmt.Println(sum)
}
