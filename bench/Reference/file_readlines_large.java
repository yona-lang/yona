import java.io.*;

public class file_readlines_large {
    public static void main(String[] args) throws Exception {
        long total = 0;
        try (BufferedReader r = new BufferedReader(new FileReader("bench/data/large_text.txt"))) {
            String line;
            while ((line = r.readLine()) != null) total += line.length();
        }
        System.out.println(total);
    }
}
