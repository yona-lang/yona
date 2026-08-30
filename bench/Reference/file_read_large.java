import java.nio.file.*;
public class file_read_large {
    public static void main(String[] args) throws Exception {
        System.out.println(Files.readString(Path.of("bench/data/large_text.txt")).length());
    }
}
