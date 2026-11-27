import std;

struct Entry { name: u8[varint], score: i64 }
let packed: u8[][varint] = ["ab", "cde"];

async fn marker(value: i64) -> i64 {
    await yield_now();
    return value;
}

async fn grow() -> i64 {
    var entries: Entry[>..] = [Entry { "a", 1 }, Entry { "longer", 2 }];
    var result = 0;
    for entry, index in entries {
        let delta = await marker(index + 1);
        entry.score += delta;
        if index == 0 { entries.push(Entry { "end", 10 }); }
        if index == 1 { continue; }
        print("entry ", entry.name, " ", entry.score);
        result += entry.score;
    }
    return result;
}

async fn walk() -> i64 {
    let left: u8[][>..] = ["a", "12345", "z"];
    let right: u8[][>..] = ["longest", "bb", "ccc", "d"];
    var view = left[..];
    var total = 0;
    for word, index in view {
        if index == 0 { view = right[..]; }
        defer { print("item ", index, " ", word); }
        await yield_now();
        if index == 1 { continue; }
        total += word.len;
        if index == 3 { break; }
    }
    return total;
}

async fn packed_sum() -> i64 {
    var sum = 0;
    for word, index in packed[..] {
        await yield_now();
        sum += word.len + index;
    }
    return sum;
}

enum Token { Text { bytes: u8[varint] }, Alias { link: Token..&<varint> } }

recursive fn text_length(token: Token..&) -> i64 {
    match token { Text &text => text.bytes.len, Alias &alias => text_length(alias.link) }
}

async fn linked() -> i64 {
    var tokens: Token..[>..] = [];
    let text = tokens.push(Token.Text { bytes: "abc" });
    tokens.push(Token.Alias { link: text });
    var total = 0;
    for token in tokens {
        await yield_now();
        total += text_length(token);
    }
    return total;
}

fn main() {
    print("grown ", grow().join());
    print("walked ", walk().join());
    task_scope(walk()) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
    print("packed ", packed_sum().join());
    print("linked ", linked().join());
}
