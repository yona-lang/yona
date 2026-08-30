class task_group_arena {
    public static void main(String[] args) {
        int acc = 0;
        for (int i = 0; i < 50000; i++) {
            acc += 10;
        }
        System.out.println(acc);
    }
}
