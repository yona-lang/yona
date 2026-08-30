package main

import "fmt"

const N = 10000

func main() {
	ch := make(chan int, 64)
	go func() {
		for i := 1; i <= N; i++ {
			ch <- i
		}
		close(ch)
	}()
	sum := 0
	for v := range ch {
		sum += v
	}
	fmt.Println(sum)
}
