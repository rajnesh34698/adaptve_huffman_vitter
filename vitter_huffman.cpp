#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
class BitWriter {
    std::vector<unsigned char> data;
    unsigned char current = 0;
    int count = 0;
    std::uint64_t totalBits = 0;

public:
    void writeBit(int bit) {
        current = static_cast<unsigned char>((current << 1) | (bit & 1));
        ++count;
        ++totalBits;
        if (count == 8) {
            data.push_back(current);
            current = 0;
            count = 0;
        }
    }

    void write(const std::string& bits) {
        for (char b : bits) writeBit(b == '1');
    }

    void finish() {
        if (count) {
            current <<= (8 - count);
            data.push_back(current);
            current = 0;
            count = 0;
        }
    }

    const std::vector<unsigned char>& bytes() const { return data; }
    std::uint64_t bitCount() const { return totalBits; }
};

class BitReader {
    const std::vector<unsigned char>& data;
    std::uint64_t totalBits;
    std::uint64_t position = 0;

public:
    BitReader(const std::vector<unsigned char>& d, std::uint64_t bits)
        : data(d), totalBits(bits) {}

    int readBit() {
        if (position >= totalBits)
            throw std::runtime_error("Unexpected end of compressed bit stream.");

        const unsigned char byte = data[position / 8];
        const int shift = 7 - static_cast<int>(position % 8);
        ++position;
        return (byte >> shift) & 1;
    }

    std::uint64_t positionBits() const { return position; }
};

class VitterHuffman {
private:
    static constexpr int ALPHABET = 256;
    static constexpr int ROOT_NUMBER = 2 * ALPHABET;
    static constexpr int INTERNAL = -1;
    static constexpr int NYT_SYMBOL = -2;

    struct Node {
        int weight = 0;
        int symbol = NYT_SYMBOL;
        int number = 0;
        Node* parent = nullptr;
        Node* left = nullptr;
        Node* right = nullptr;

        bool isLeaf() const { return left == nullptr && right == nullptr; }
    };

    std::vector<std::unique_ptr<Node>> storage;
    std::array<Node*, ROOT_NUMBER + 1> byNumber{};
    std::array<Node*, ALPHABET> symbolNode{};

    Node* root = nullptr;
    Node* nyt = nullptr;
    int nextNumber = ROOT_NUMBER;

    Node* createNode() {
        storage.push_back(std::make_unique<Node>());
        return storage.back().get();
    }

    static std::string fixedCode(unsigned char c) {
        std::string result(8, '0');
        unsigned value = c;
        for (int i = 7; i >= 0; --i)
            result[7 - i] = char('0' + ((value >> i) & 1u));
        return result;
    }

    void createNewSymbol(unsigned char c) {
        Node* oldNYT = nyt;
        Node* newNYT = createNode();
        Node* leaf = createNode();

        oldNYT->symbol = INTERNAL;
        oldNYT->left = newNYT;
        oldNYT->right = leaf;

        newNYT->parent = oldNYT;
        leaf->parent = oldNYT;
        leaf->symbol = c;

        oldNYT->number = nextNumber--;
        leaf->number = nextNumber--;

        byNumber[oldNYT->number] = oldNYT;
        byNumber[leaf->number] = leaf;

        symbolNode[c] = leaf;
        nyt = newNYT;
    }

    // Interchange two nodes while preserving their implicit numbers.
    void interchange(Node* a, Node* b) {
        if (!a || !b || a == b) return;

        if (a->parent == b->parent) {
            Node* p = a->parent;
            if (p->left == a) {
                p->left = b;
                p->right = a;
            } else {
                p->left = a;
                p->right = b;
            }
        } else {
            Node* pa = a->parent;
            Node* pb = b->parent;

            if (pa->left == a) pa->left = b;
            else pa->right = b;

            if (pb->left == b) pb->left = a;
            else pb->right = a;

            a->parent = pb;
            b->parent = pa;
        }

        const int na = a->number;
        const int nb = b->number;
        std::swap(byNumber[na], byNumber[nb]);
        a->number = nb;
        b->number = na;
    }

    // Highest-numbered node in the same Vitter block.
    Node* blockLeader(Node* node) const {
        Node* highest = nullptr;

        for (int i = node->number + 1; i < ROOT_NUMBER; ++i) {
            Node* candidate = byNumber[i];
            if (!candidate) continue;

            if (candidate->weight != node->weight)
                break;

            if (candidate == node->parent)
                continue;

            // A leaf belongs to a leaf block; an internal node belongs
            // to an internal block.
            if (node->isLeaf() && candidate->symbol == INTERNAL)
                break;

            highest = candidate;
        }
        return highest;
    }

    // Equivalent explicit-tree implementation of Vitter's slide.
    void slideNode(Node* node) {
        // Leaf of weight w slides through internal nodes of weight w.
        // Internal node of weight w slides through leaves of weight w+1.
        const int targetWeight = node->isLeaf()
            ? node->weight
            : node->weight + 1;

        for (int i = node->number + 1; i < ROOT_NUMBER; ++i) {
            Node* candidate = byNumber[i];
            if (!candidate) continue;

            if (targetWeight < candidate->weight)
                break;

            if (candidate == node->parent)
                continue;

            // Internal node becomes the first node in the target
            // internal block, rather than the leader of that block.
            if (node->symbol == INTERNAL &&
                candidate->symbol == INTERNAL &&
                targetWeight == candidate->weight)
                break;

            // Repeated interchanges simulate Vitter's slide.
            interchange(node, candidate);
        }
    }

    // Vitter's Slide-and-Increment operation.
    Node* slideAndIncrement(Node* node) {
        Node* oldParent = node->parent;
        slideNode(node);
        ++node->weight;

        if (node->isLeaf())
            return node->parent; // new parent of the leaf
        return oldParent;        // former parent of the internal node
    }

public:
    VitterHuffman() { reset(); }

    void reset() {
        storage.clear();
        byNumber.fill(nullptr);
        symbolNode.fill(nullptr);

        nextNumber = ROOT_NUMBER;
        root = createNode();
        root->number = ROOT_NUMBER;
        root->symbol = NYT_SYMBOL;
        nyt = root;
        byNumber[ROOT_NUMBER] = root;
    }

    std::string code(Node* node) const {
        std::string result;
        while (node && node->parent) {
            result.push_back(node->parent->left == node ? '0' : '1');
            node = node->parent;
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

    // ------------------------------------------------------------
    // This is the actual Vitter Algorithm V update procedure.
    // ------------------------------------------------------------
    void update(unsigned char c) {
        Node* node = symbolNode[c];
        Node* leafToIncrement = nullptr;

        if (node == nullptr) {
            // New symbol: split the NYT node.
            createNewSymbol(c);
            node = nyt->parent;          // newly created internal node
            leafToIncrement = node->right; // strict Vitter update
        } else {
            // Existing symbol: interchange with its block leader.
            Node* leader = blockLeader(node);
            if (leader)
                interchange(node, leader);

            // Special case: symbol is sibling of the NYT node.
            if (node->parent == nyt->parent) {
                leafToIncrement = node;
                node = node->parent;
            }
        }

        // Main ancestor update using Slide-and-Increment.
        while (node != root)
            node = slideAndIncrement(node);

        // Finish the special leaf update.
        if (leafToIncrement)
            slideAndIncrement(leafToIncrement);
    }

    // Returns {code, isNewSymbol}.
    std::pair<std::string, bool> encodeByte(unsigned char c) {
        Node* node = symbolNode[c];

        if (node == nullptr) {
            const std::string result = code(nyt) + fixedCode(c);
            update(c);
            return {result, true};
        }

        const std::string result = code(node);
        update(c);
        return {result, false};
    }

    unsigned char decodeByte(BitReader& reader) {
        Node* node = root;

        while (!node->isLeaf()) {
            const int bit = reader.readBit();
            node = bit ? node->right : node->left;
        }

        unsigned char c;
        if (node->symbol == NYT_SYMBOL) {
            unsigned value = 0;
            for (int i = 0; i < 8; ++i)
                value = (value << 1) | unsigned(reader.readBit());
            c = static_cast<unsigned char>(value);
        } else {
            c = static_cast<unsigned char>(node->symbol);
        }

        update(c);
        return c;
    }
};

static constexpr char MAGIC[] = "AHUF1";

void writeU64BE(std::ofstream& out, std::uint64_t value) {
    for (int i = 7; i >= 0; --i)
        out.put(static_cast<char>((value >> (8 * i)) & 0xFFu));
}

std::uint64_t readU64BE(std::ifstream& in) {
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        const int c = in.get();
        if (c == EOF)
            throw std::runtime_error("Incomplete .huff header.");
        value |= std::uint64_t(static_cast<unsigned char>(c)) << (8 * i);
    }
    return value;
}

void compressFile(const std::string& inputPath, const std::string& outputPath) {
    std::ifstream in(inputPath, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open input file: " + inputPath);

    std::vector<unsigned char> input(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());

    VitterHuffman codec;
    BitWriter writer;

    for (unsigned char c : input)
        writer.write(codec.encodeByte(c).first);

    writer.finish();

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot create output file: " + outputPath);

    out.write(MAGIC, 5);
    writeU64BE(out, input.size());
    writeU64BE(out, writer.bitCount());

    const auto& payload = writer.bytes();
    if (!payload.empty())
        out.write(reinterpret_cast<const char*>(payload.data()), payload.size());

    std::cout << "Compressed successfully.\n"
              << "Original size   : " << input.size() << " bytes\n"
              << "Compressed size : " << (21 + payload.size()) << " bytes\n"
              << "Encoded bits    : " << writer.bitCount() << "\n";
}

void decompressFile(const std::string& inputPath, const std::string& outputPath) {
    std::ifstream in(inputPath, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open compressed file: " + inputPath);

    char magic[5];
    in.read(magic, 5);
    if (in.gcount() != 5 || std::string(magic, 5) != std::string(MAGIC, 5))
        throw std::runtime_error("Invalid Adaptive Huffman file.");

    const std::uint64_t originalSize = readU64BE(in);
    const std::uint64_t totalBits = readU64BE(in);

    std::vector<unsigned char> payload(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());

    if (totalBits > std::uint64_t(payload.size()) * 8)
        throw std::runtime_error("Invalid payload bit count.");

    VitterHuffman codec;
    BitReader reader(payload, totalBits);

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot create output file: " + outputPath);

    for (std::uint64_t i = 0; i < originalSize; ++i)
        out.put(static_cast<char>(codec.decodeByte(reader)));

    std::cout << "Decompressed successfully.\n"
              << "Output size : " << originalSize << " bytes\n";
}

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string(argv[1]) == "demo") {
            // demo();
            return 0;
        }

        if (argc != 4) {
            std::cout << "Vitter Adaptive Huffman (Algorithm V)\n\n"
                      << "Usage:\n"
                      << "  " << argv[0] << " demo\n"
                      << "  " << argv[0] << " compress <input> <output.huff>\n"
                      << "  " << argv[0] << " decompress <input.huff> <output>\n";
            return 0;
        }

        const std::string command = argv[1];
        if (command == "compress")
            compressFile(argv[2], argv[3]);
        else if (command == "decompress")
            decompressFile(argv[2], argv[3]);
        else
            throw std::runtime_error("Unknown command: " + command);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
