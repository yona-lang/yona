import java.util.HashMap;
import java.util.Map;

public class dict_build {
    public static void main(String[] args) {
        Map<Integer, Integer> d = new HashMap<>();
        for (int n = 10000; n >= 1; n--) d.put(n, n * n);
        System.out.println(d.size());
    }
}
