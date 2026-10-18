// The C++ YAML-subset reader (netproto/yaml.hpp) and load_catalog_yaml (CAP-E02).
// The reader is checked three ways: unit cases (what it accepts, what it must reject with a line number),
// the checked-in proto files, and a differential test against PyYAML (ctest `yaml_differential`, which
// runs netproto_yaml_dump over every proto file and compares with yaml.safe_load).
#include <regex>

#include "netproto/yaml.hpp"
#include "real_catalog.hpp"
#include "generated/catalog_client_packets.hpp"
#include "harness.hpp"

using namespace netproto;
using namespace fx;
using yaml::Node;

namespace {
bool parse_fails(const std::string& text, const char* needle) {
  try { (void)yaml::parse(text); } catch (const SpecError& e) { return std::string(e.what()).find(needle) != std::string::npos; }
  return false;
}
}  // namespace

TEST("CAP-E02", yaml_scalars_and_comments) {
  const Node n = yaml::parse("a: 1  # comment\nb: 0x0064\nc: -7\nd: \"x # not a comment\"\ne: 'it''s'\nf: true\ng: null\nh: ~\ni:\nj: plain text\nk: 007\n");
  CHECK(n.at("a").i == 1 && n.at("b").i == 0x64 && n.at("c").i == -7);
  CHECK(n.at("d").s == "x # not a comment" && n.at("e").s == "it's");
  CHECK(n.at("f").kind == Node::Kind::Bool && n.at("f").b);
  CHECK(n.at("g").is_null() && n.at("h").is_null() && n.at("i").is_null());
  CHECK(n.at("j").s == "plain text");
  CHECK(n.at("k").kind == Node::Kind::Str && n.at("k").s == "007");  // not YAML 1.1 octal
}

TEST("CAP-E02", yaml_block_and_flow_collections) {
  const Node n = yaml::parse(
      "list:\n  - 1\n  - { name: \"a b\", offset: 0x10, tags: [x, y, [z]] }\n  - key: v\n    other: [ ]\nsame:\n- 1\n- 2\nnested:\n  deep:\n    - a: 1\n      b: 2\n    - a: 3\n");
  const Node& l = n.at("list");
  CHECK(l.seq.size() == 3 && l.seq[0].i == 1);
  CHECK(l.seq[1].at("name").s == "a b" && l.seq[1].at("offset").i == 16);
  CHECK(l.seq[1].at("tags").seq.size() == 3 && l.seq[1].at("tags").seq[2].seq[0].s == "z");
  CHECK(l.seq[2].at("key").s == "v" && l.seq[2].at("other").seq.empty());
  CHECK(n.at("same").seq.size() == 2);  // a sequence at the indentation of its key
  CHECK(n.at("nested").at("deep").seq[0].at("b").i == 2 && n.at("nested").at("deep").seq[1].at("a").i == 3);
}

TEST("CAP-E02", yaml_rejects_what_it_does_not_support_with_a_line_number) {
  CHECK(parse_fails("a: 1\n\tb: 2\n", "line 2: tab"));
  CHECK(parse_fails("a: 1\na: 2\n", "duplicate key 'a'"));
  CHECK(parse_fails("a: [1, 2\n", "unterminated"));
  CHECK(parse_fails("a: \"abc\n", "unterminated"));
  CHECK(parse_fails("a: &x 1\n", "unsupported YAML feature"));
  CHECK(parse_fails("a: |\n  text\n", "unsupported YAML feature"));
  CHECK(parse_fails("a: 1\n---\nb: 2\n", "multiple documents"));
  CHECK(parse_fails("a: 1\n  b: 2\n", "line 2"));
  CHECK(parse_fails("{ a: 1 } trailing\n", "line 1"));
  CHECK(parse_fails("a: { b: 1, b: 2 }\n", "duplicate key 'b'"));
  CHECK(parse_fails("a: \"bad \\q escape\"\n", "unsupported escape"));
}

TEST("CAP-E02", yaml_deep_flow_nesting_is_bounded) {
  std::string deep = "a: ";
  for (int i = 0; i < 200; ++i) deep += "[";
  for (int i = 0; i < 200; ++i) deep += "]";
  CHECK(parse_fails(deep + "\n", "nested too deeply"));
}

TEST("CAP-E02", yaml_spec_loads_and_equals_the_generated_catalog) {
  // The runtime-loaded file and the table compiled in from the same file must describe the same layouts.
  const auto loaded = load_catalog_yaml(std::string(NETPROTO_SOURCE_DIR) + "/proto/client_packets.yaml");
  const auto gen = generated::client_packets_catalog();
  CHECK(loaded.packets().size() == gen.packets().size());
  for (std::size_t i = 0; i < gen.packets().size(); ++i) {
    const auto &a = loaded.packets()[i], &b = gen.packets()[i];
    CHECK(a.name == b.name && a.direction == b.direction && a.gate == b.gate && a.category == b.category);
    CHECK(a.versions.size() == b.versions.size());
    for (std::size_t k = 0; k < a.versions.size(); ++k) {
      const auto &x = a.versions[k], &y = b.versions[k];
      CHECK(x.opcode == y.opcode && x.fields_known == y.fields_known && x.handler_offsets == y.handler_offsets);
      CHECK(x.length.kind == y.length.kind && x.length.fixed_size == y.length.fixed_size);
      CHECK(x.fields.size() == y.fields.size());
      for (std::size_t f = 0; f < x.fields.size(); ++f)
        CHECK(x.fields[f].name == y.fields[f].name && x.fields[f].offset == y.fields[f].offset &&
              x.fields[f].type == y.fields[f].type && x.fields[f].array_len == y.fields[f].array_len);
    }
  }
}

TEST("CAP-E02", yaml_full_catalog_loads_and_equals_the_generated_catalog) {
  const auto loaded = load_catalog_yaml(std::string(NETPROTO_SOURCE_DIR) + "/proto/packets_re_20211103.yaml");
  const PacketCatalog& gen = full_catalog();
  CHECK(loaded.packets().size() == gen.packets().size() && loaded.packets().size() > 1000);
  std::size_t fields = 0;
  for (std::size_t i = 0; i < gen.packets().size(); ++i) {
    const auto &a = loaded.packets()[i], &b = gen.packets()[i];
    CHECK(a.name == b.name && a.direction == b.direction && a.gate == b.gate && a.category == b.category);
    const auto &x = a.versions.at(0), &y = b.versions.at(0);
    CHECK(x.opcode == y.opcode && x.fields_known == y.fields_known && x.source == y.source && x.handler_offsets == y.handler_offsets);
    CHECK(x.length.kind == y.length.kind && x.length.fixed_size == y.length.fixed_size &&
          x.length.prefix_offset == y.length.prefix_offset && x.length.header_size == y.length.header_size);
    CHECK(x.length.repeat.has_value() == y.length.repeat.has_value());
    if (x.length.repeat) CHECK(x.length.repeat->offset == y.length.repeat->offset && x.length.repeat->stride == y.length.repeat->stride);
    CHECK(x.fields.size() == y.fields.size());
    for (std::size_t f = 0; f < x.fields.size(); ++f, ++fields) {
      const auto &p = x.fields[f], &q = y.fields[f];
      CHECK(p.name == q.name && p.offset == q.offset && p.type == q.type && p.width == q.width && p.array_len == q.array_len &&
            p.row == q.row && p.bit_shift == q.bit_shift && p.bit_width == q.bit_width);
    }
  }
  CHECK(fields > 1900);
  // and the loaded catalog drives the same codec
  const auto set = CodecSet::build(loaded, kRe());
  CHECK(set.packet_count() == full_set().packet_count());
}

TEST("CAP-E02", yaml_loader_rejects_unknown_keys_bad_types_and_duplicate_packets) {
  const std::string head = "schema: 1\npackets:\n  - name: A\n    direction: cz\n    versions:\n      - opcode: 0x0100\n        length: { fixed: 4 }\n";
  CHECK(load_catalog_yaml_text(head + "        fields: unknown\n").packets().size() == 1);
  auto fails = [](const std::string& t, const char* needle) {
    try { (void)load_catalog_yaml_text(t); } catch (const SpecError& e) { return std::string(e.what()).find(needle) != std::string::npos; }
    return false;
  };
  CHECK(fails(head + "        feilds: unknown\n", "unknown key 'feilds'"));
  CHECK(fails(head + "        fields:\n          - { name: x, offset: 2, type: u24 }\n", "unknown field type 'u24'"));
  CHECK(fails(head + "        fields:\n          - { name: x, offset: 2, type: u8, wdith: 3 }\n", "unknown key 'wdith'"));
  CHECK(fails(head + "  - name: A\n    direction: cz\n    versions: [ { opcode: 1, length: { fixed: 2 } } ]\n", "duplicate packet 'A'"));
  CHECK(fails("schema: 2\npackets: []\n", "unsupported schema"));
  CHECK(fails(head.substr(0, head.find("direction")) + "direction: sideways\n    versions: []\n", "bad direction"));
  CHECK(fails(head + "        applies: \"LINUX>=1\"\n", "bad version predicate"));
}

TEST("SEC-S1", full_catalog_keeps_obfuscation_keys_out_of_the_data) {
  const auto y = slurp_file("proto/packets_re_20211103.yaml");
  CHECK(y.find("keys_env: NETPROTO_KEYS_RE_20211103") != std::string::npos);
  CHECK(!std::regex_search(y, std::regex(R"(keys\s*:\s*\[?\s*(0x)?[0-9a-fA-F]{6,})")));
  const auto cvs = load_client_versions_yaml(std::string(NETPROTO_SOURCE_DIR) + "/proto/packets_re_20211103.yaml");
  CHECK(cvs.size() == 1 && cvs[0].version == kRe() && cvs[0].keys_env == "NETPROTO_KEYS_RE_20211103");
  const auto cv2 = load_client_versions_yaml(std::string(NETPROTO_SOURCE_DIR) + "/proto/client_packets.yaml");
  CHECK(cv2.size() == 2 && cv2[1].hypothetical);
  // a spec file that tries to carry key values is refused by the loader
  bool refused = false;
  try { (void)load_client_versions_yaml_text("client_versions:\n  - { id: \"re/20211103\", flavour: re, date: 20211103, obfuscation: { keys: [1, 2, 3] } }\n"); }
  catch (const SpecError&) { refused = true; }
  CHECK(refused);
}
