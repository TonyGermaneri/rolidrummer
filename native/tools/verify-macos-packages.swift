// Read the same package classification Finder uses; never add FinderInfo attributes
// to signed code (those attributes invalidate strict signature verification).
import AppKit
import Darwin
import Foundation

var results = [[String: Any]]()
for path in CommandLine.arguments.dropFirst() {
    let url = URL(fileURLWithPath: path)
    do {
        let values = try url.resourceValues(forKeys: [.isPackageKey, .isDirectoryKey])
        guard values.isDirectory == true, values.isPackage == true,
              NSWorkspace.shared.isFilePackage(atPath: path) else {
            fputs("macOS does not recognize this directory as an opaque package: \(path)\n", stderr)
            exit(1)
        }
        results.append(["name": url.lastPathComponent, "finderPackage": true])
    } catch {
        fputs("Cannot inspect package \(path): \(error)\n", stderr)
        exit(1)
    }
}
if results.isEmpty { fputs("Supply at least one bundle path\n", stderr); exit(1) }
let data = try JSONSerialization.data(withJSONObject: results, options: [.sortedKeys])
print(String(decoding: data, as: UTF8.self))
