#!/usr/bin/env ruby
# Ruby C拡張 vs Dir.glob ベンチマーク比較

require 'benchmark'
require_relative '../benchmark_ext/rbcglob_ext'

patterns = [
  { pattern: "*.md", desc: "Simple wildcard" },
  { pattern: "tests/*.c", desc: "Single directory" },
  { pattern: "tests/**/*.c", desc: "Recursive with filter" },
  { pattern: "tests/**/*", desc: "Recursive all (4918 files)" },
  { pattern: "src/rbcglob/*.c", desc: "Deep literal path" }
]

iterations = 100

puts "=" * 70
puts "Ruby Dir.glob vs Rbcglob C Extension Benchmark"
puts "=" * 70
puts

patterns.each do |tc|
  pattern = tc[:pattern]
  desc = tc[:desc]

  # Warm up
  Dir.glob(pattern)
  Rbcglob.glob(pattern)

  puts "Pattern: #{pattern}"
  puts "  Description: #{desc}"

  # Verify correctness
  ruby_results = Dir.glob(pattern).sort
  rbcglob_results = Rbcglob.glob(pattern).sort

  if ruby_results != rbcglob_results
    puts "  ⚠️  MISMATCH!"
    puts "    Dir.glob: #{ruby_results.size} files"
    puts "    Rbcglob:  #{rbcglob_results.size} files"
    if ruby_results.size < 20 && rbcglob_results.size < 20
      puts "    Ruby:    #{ruby_results.inspect}"
      puts "    Rbcglob: #{rbcglob_results.inspect}"
    end
  else
    puts "  ✓ Results match (#{ruby_results.size} files)"
  end

  # Benchmark
  ruby_time = Benchmark.measure do
    iterations.times { Dir.glob(pattern) }
  end

  rbcglob_time = Benchmark.measure do
    iterations.times { Rbcglob.glob(pattern) }
  end

  ruby_ms = (ruby_time.real * 1000 / iterations).round(2)
  rbcglob_ms = (rbcglob_time.real * 1000 / iterations).round(2)
  speedup = (ruby_ms / rbcglob_ms).round(2)

  puts "  Dir.glob:  #{ruby_ms}ms"
  puts "  Rbcglob:   #{rbcglob_ms}ms"

  if speedup > 1
    puts "  ⚡ Rbcglob is #{speedup}x faster!"
  elsif speedup < 1
    puts "  ⚠️  Dir.glob is #{(1/speedup).round(2)}x faster"
  else
    puts "  ≈ Similar performance"
  end

  puts
end
