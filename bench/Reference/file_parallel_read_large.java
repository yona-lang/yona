import java.nio.file.*;
import java.util.concurrent.*;
import java.util.stream.*;
public class file_parallel_read_large {
    public static void main(String[] args) throws Exception {
        try (var ex = Executors.newVirtualThreadPerTaskExecutor()) {
            var futures = IntStream.rangeClosed(1, 4)
                .mapToObj(i -> ex.submit(() -> (long) Files.readString(Path.of("bench/data/chunk_" + i + ".txt")).length()))
                .toList();
            long total = 0;
            for (var f : futures) total += f.get();
            System.out.println(total);
        }
    }
}
