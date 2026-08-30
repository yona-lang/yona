import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class list_reverse {
    public static void main(String[] args) {
        List<Integer> xs = new ArrayList<>();
        for (int n = 10000; n >= 1; n--) xs.add(n);
        Collections.reverse(xs);
        System.out.println(xs.size());
    }
}
