import java.nio.file.*;
import java.util.concurrent.*;
import java.util.stream.*;

public class file_parallel_read {
    public static void main(String[] args) throws Exception {
        String path = "bench/data/bench_text.txt";
        try (var ex = Executors.newVirtualThreadPerTaskExecutor()) {
            var futures = IntStream.range(0, 3)
                .mapToObj(i -> ex.submit(() -> Files.readString(Path.of(path)).length()))
                .toList();
            long total = 0;
            for (var f : futures) total += f.get();
            System.out.println(total);
        }
    }
}
