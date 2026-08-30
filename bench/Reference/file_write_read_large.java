import java.nio.file.*;

public class file_write_read_large {
    public static void main(String[] args) throws Exception {
        byte[] data = Files.readAllBytes(Paths.get("bench/data/large_text.txt"));
        Files.write(Paths.get("/tmp/java_bench_large_write.txt"), data);
        byte[] data2 = Files.readAllBytes(Paths.get("/tmp/java_bench_large_write.txt"));
        System.out.println(data2.length);
    }
}
