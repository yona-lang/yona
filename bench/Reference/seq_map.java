public class seq_map {
    public static void main(String[] args) {
        int[] xs = new int[20];
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < 20; i++) {
            int x = i + 1;
            int c = x * x * x;
            if (i > 0) sb.append(", ");
            sb.append(c);
        }
        sb.append("]");
        System.out.println(sb.toString());
    }
}
