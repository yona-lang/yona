import java.util.ArrayList;
import java.util.List;

public class list_map_filter {
    public static void main(String[] args) {
        List<Integer> nums = new ArrayList<>();
        for (int n = 10000; n >= 1; n--) nums.add(n);
        long s = 0;
        for (int x : nums) {
            int d = x * 2;
            if (d % 4 == 0) s += d;
        }
        System.out.println(s);
    }
}
