import java.util.HashSet;
import java.util.Set;

public class set_build {
    public static void main(String[] args) {
        Set<Integer> s = new HashSet<>();
        for (int n = 10000; n >= 1; n--) s.add(n);
        System.out.println(s.size());
    }
}
