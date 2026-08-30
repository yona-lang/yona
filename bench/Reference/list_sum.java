import java.util.ArrayList;
import java.util.List;

public class list_sum {
    public static void main(String[] args) {
        List<Integer> xs = new ArrayList<>();
        for (int n = 10000; n >= 1; n--) xs.add(n);
        long s = 0;
        for (int x : xs) s += x;
        System.out.println(s);
    }
}
