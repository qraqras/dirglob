#!/usr/bin/env ruby

# Rubyでのエスケープ文字の動作を確認

patterns = [
  'tests/fixtures/08_escapechars/\*asterisk.txt',
  'tests/fixtures/08_escapechars/\[brackets\].txt',
  'tests/fixtures/08_escapechars/*asterisk.txt',
  'tests/fixtures/08_escapechars/[brackets].txt',
]

patterns.each do |pattern|
  results = Dir.glob(pattern)
  puts "Pattern: #{pattern.inspect}"
  puts "  Count: #{results.length}"
  results.each { |r| puts "    - #{r}" }
  puts
end
