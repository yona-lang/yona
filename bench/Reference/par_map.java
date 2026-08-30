import java.util.concurrent.*;
import java.util.stream.*;

class par_map {
    public static void main(String[] args) {
        long[] results = IntStream.rangeClosed(1, 20)
            .parallel()
            .mapToLong(i -> (long)i * i * i)
            .toArray();
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < results.length; i++) {
            if (i > 0) sb.append(", ");
            sb.append(results[i]);
        }
        sb.append("]");
        System.out.println(sb);
    }
}
