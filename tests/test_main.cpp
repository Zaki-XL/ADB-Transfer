#include <iostream>

void TestConfigManager();
void TestStorageScanner();
void TestTransferEngine();
void TestI18nManager();

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "       ADB Transfer Unit Tests          " << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        TestConfigManager();
        TestStorageScanner();
        TestTransferEngine();
        TestI18nManager();

        std::cout << "========================================" << std::endl;
        std::cout << " ALL TESTS PASSED SUCCESSFULLY! (4/4)   " << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[ERROR] Exception caught: " << ex.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "[ERROR] Unknown exception occurred." << std::endl;
        return 1;
    }
}

int wmain() {
    return main();
}

