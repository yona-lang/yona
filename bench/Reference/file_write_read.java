import java.nio.file.*;
class file_write_read {
    public static void main(String[] args) throws Exception {
        String data = Files.readString(Path.of("bench/data/bench_text.txt"));
        Files.writeString(Path.of("/tmp/yona_bench_write_java.txt"), data);
        String data2 = Files.readString(Path.of("/tmp/yona_bench_write_java.txt"));
        System.out.println(data2.length());
    }
}
