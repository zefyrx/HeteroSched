#include <iostream>
#include <vector>

#include "protocol/registration.h"

bool testNormalRegistration() {
    const auto worker =
        parseRegistration("REGISTER_WORKER 1 8 16384");

    if (!worker) {
        std::cerr << "FAIL: valid registration rejected\n";
        return false;
    }

    const Resource& total = worker->getTotalResource();
    const Resource& available = worker->getAvailableResource();

    if (worker->getId() != 1 ||
        total.cpuCores != 8 ||
        total.memoryMB != 16384 ||
        available.cpuCores != 8 ||
        available.memoryMB != 16384) {
        std::cerr << "FAIL: incorrect Worker data\n";
        return false;
    }

    return true;
}

bool testAllowsWorkerIdZero() {
    const auto worker =
        parseRegistration("REGISTER_WORKER 0 8 16384");

    if (!worker) {
        std::cerr << "FAIL: valid registration rejected\n";
        return false;
    }

    const Resource& total = worker->getTotalResource();
    const Resource& available = worker->getAvailableResource();

    if (worker->getId() != 0 ||
        total.cpuCores != 8 ||
        total.memoryMB != 16384 ||
        available.cpuCores != 8 ||
        available.memoryMB != 16384) {
        std::cerr << "FAIL: incorrect Worker data\n";
        return false;
    }

    return true;
}

bool testAllowsSignedAdjacentFields() {
    const auto worker =
        parseRegistration("REGISTER_WORKER 1+8+16384");

    if (!worker) {
        std::cerr << "FAIL: valid registration rejected\n";
        return false;
    }

    const Resource& total = worker->getTotalResource();
    const Resource& available = worker->getAvailableResource();

    if (worker->getId() != 1 ||
        total.cpuCores != 8 ||
        total.memoryMB != 16384 ||
        available.cpuCores != 8 ||
        available.memoryMB != 16384) {
        std::cerr << "FAIL: incorrect Worker data\n";
        return false;
    }

    return true;
}

bool testRejectsInvalidRegistrations() {
    std::vector<std::string> InvalidStrings = {
        "",
        "HELLO 1 8 16384",
        "REGISTER_WORKER 1 8",
        "REGISTER_WORKER 1 8 16384 extra",
        "REGISTER_WORKER -1 8 16384",
        "REGISTER_WORKER 1 0 16384",
        "REGISTER_WORKER 1 8 -1",
        "REGISTER_WORKER 1 eight 16384",
        "REGISTER_WORKER 1 8 16384abc",
       " REGISTER_WORKER 1 8 999999999999999999999"
    };

    for (std::string w : InvalidStrings) {
        const auto worker = parseRegistration(w);

        if (worker) {
            std::cerr << "FAIL: Invalid registration accepted\n";
            return false;
        }
    }

    return true;
}

int main() {
    if (!testNormalRegistration()) {
        return 1;
    }

    if (!testAllowsSignedAdjacentFields()) {
        return 1;
    }

    if (!testAllowsWorkerIdZero()) {
        return 1;
    }

    if (!testRejectsInvalidRegistrations()) {
        return 1;
    }

    std::cout << "PASS: registration tests passed\n";
    return 0;
}