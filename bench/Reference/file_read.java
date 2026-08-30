import java.nio.file.*;
class file_read {
    public static void main(String[] args) throws Exception {
        String data = Files.readString(Path.of("bench/data/bench_text.txt"));
        System.out.println(data.length());
    }
}
