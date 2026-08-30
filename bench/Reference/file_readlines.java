import java.nio.file.*;
class file_readlines {
    public static void main(String[] args) throws Exception {
        long count = Files.lines(Path.of("bench/data/bench_text.txt")).count();
        System.out.println(count);
    }
}
