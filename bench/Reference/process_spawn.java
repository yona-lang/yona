import java.io.*;

public class process_spawn {
    public static void main(String[] args) throws Exception {
        Process p = Runtime.getRuntime().exec(new String[]{"seq", "1", "10000"});
        String output = new String(p.getInputStream().readAllBytes());
        p.waitFor();
        System.out.println(output.length());
    }
}
