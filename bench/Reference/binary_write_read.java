import java.io.*;
import java.nio.file.*;

class binary_write_read {
    public static void main(String[] args) throws Exception {
        byte[] data = Files.readAllBytes(Path.of("bench/data/bench_binary.bin"));
        Files.write(Path.of("/tmp/java_bench_binary_wr.bin"), data);
        byte[] data2 = Files.readAllBytes(Path.of("/tmp/java_bench_binary_wr.bin"));
        System.out.println(data2.length);
    }
}
