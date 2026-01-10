# encoding: utf-8

def test_enc(name, pattern_str, target_str, enc)
  pattern = pattern_str.encode(enc)
  target = target_str.encode(enc)
  matched = File.fnmatch(pattern, target)
  puts "#{name} (#{enc}): #{matched} (bytes: #{pattern.bytesize} vs #{target.bytesize})"
end

puts "=== File.fnmatch Character-wise Matching Test ==="

# UTF-8
test_enc("UTF-8", "?", "あ", "UTF-8")

# EUC-JP
begin
  test_enc("EUC-JP", "?", "あ", "EUC-JP")
rescue => e
  puts "EUC-JP failed: #{e}"
end

# WINDOWS-31J (Shift_JIS)
begin
  test_enc("Windows-31J", "?", "あ", "Windows-31J")
rescue => e
  puts "Windows-31J failed: #{e}"
end

# Binary
test_enc("BINARY", "?", "a", "BINARY")
test_enc("BINARY", "?", "あ", "BINARY") # Should fail because 'あ' is 3 bytes in UTF-8 (source) interpreted as binary
