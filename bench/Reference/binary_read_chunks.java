import java.io.*;

class binary_read_chunks {
    public static void main(String[] args) throws Exception {
        FileInputStream fis = new FileInputStream("bench/data/bench_binary.bin");
        byte[] buf = new byte[65536];
        long total = 0;
        int n;
        while ((n = fis.read(buf)) > 0)
            total += n;
        fis.close();
        System.out.println(total);
    }
}
