// Parallel exec to match Yona's let-parallelism
import java.io.*;
import java.util.concurrent.*;
import java.util.stream.*;

public class process_exec {
    static int execLen(String cmd) throws Exception {
        Process p = Runtime.getRuntime().exec(new String[]{"sh", "-c", cmd});
        String out = new String(p.getInputStream().readAllBytes()).trim();
        p.waitFor();
        return out.length();
    }

    public static void main(String[] args) throws Exception {
        try (var ex = Executors.newVirtualThreadPerTaskExecutor()) {
            var futures = Stream.of("echo hello", "echo world", "echo yona")
                .map(cmd -> ex.submit(() -> execLen(cmd)))
                .toList();
            int total = 0;
            for (var f : futures) total += f.get();
            System.out.println(total);
        }
    }
}
