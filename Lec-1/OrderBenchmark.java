public class OrderBenchmark {

    static volatile long counter = 0;

    static void sendOrder() {
        counter = counter + 1;
    }

    public static void main(String[] args) {

        int num_of_orders = 1000_000_00;

        for (int i = 0; i < num_of_orders; i++) {
            sendOrder();
        }
    }
}
