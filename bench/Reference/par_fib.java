// Parallel fibonacci: 8 x fib(30) using virtual threads
import java.util.concurrent.*;
import java.util.stream.*;

public class par_fib {
    static long fib(int n) {
        if (n <= 1) return n;
        return fib(n - 1) + fib(n - 2);
    }

    public static void main(String[] args) throws Exception {
        try (var executor = Executors.newVirtualThreadPerTaskExecutor()) {
            var futures = IntStream.range(0, 8)
                .mapToObj(i -> executor.submit(() -> fib(30)))
                .toList();
            long total = 0;
            for (var f : futures) total += f.get();
            System.out.println(total);
        }
    }
}
