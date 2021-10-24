import Foundation

extension Collection {
    /// Returns the element at the specified index if it is within bounds, otherwise nil.
    subscript (safe index: Index) -> Element? {
        return indices.contains(index) ? self[index] : nil
    }
}

extension Optional {
    func filter(_ predicate: (Wrapped) throws -> Bool) rethrows -> Optional {
        return try flatMap { try predicate($0) ? self : nil }
    }

    func exists(_ predicate: (Wrapped) throws -> Bool) rethrows -> Bool {
        return try map { try predicate($0) } ?? false
    }
}

extension String {
    func r(options: NSRegularExpression.Options = []) -> NSRegularExpression {
        return try! NSRegularExpression(pattern: self, options: options)
    }

    func matches(_ regex: NSRegularExpression) -> Bool {
        return regex.isMatch(in: self)
    }

    func fullRange() -> NSRange {
        return NSRange(startIndex..<endIndex, in: self)
    }
}

extension NSRegularExpression {
    func isMatch(in str: String, options: NSRegularExpression.MatchingOptions = []) -> Bool {
        return self.firstMatch(in: str, options: options, range: str.fullRange()) != nil
    }

    func group(in str: String, at: Int) -> String? {
        if let match = self.firstMatch(in: str, options: [], range: str.fullRange()) {
            let matchRange = match.range(at: at)
            if matchRange.location != NSNotFound,
               let sourceRange = Range(matchRange, in: str),
               !sourceRange.isEmpty {
                return String(str[sourceRange])
            }
        }
        return nil
    }
}
