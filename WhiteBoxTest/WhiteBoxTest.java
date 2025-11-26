import jdk.test.whitebox.WhiteBox;

public class WhiteBoxTest {

    private static final WhiteBox WB = WhiteBox.getWhiteBox();


    public static void main(String[] args) {

        System.out.println("Test 3");
        WB.fullGC();
        WB.fullGC();
        System.out.println(WB.g1CompletedConcurrentMarkCycles());
        System.out.println(WB.g1RegionSize());
    }



}
