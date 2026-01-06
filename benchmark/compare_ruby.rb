#!/usr/bin/env ruby
# Ruby vs rbcglob comparison

require 'benchmark'

patterns = [
  { pattern: "tests/**/*", desc: "Recursive all (4919 files)" }
]

iterations = 100

patterns.each do |tc|
  pattern = tc[:pattern]
  desc = tc[:desc]

  puts "Pattern: #{pattern} - #{desc}"

  ruby_time = Benchmark.measure do
    iterations.times { Dir.glob(pattern) }
  end

  ruby_ms = (ruby_time.real * 1000 / iterations).round(2)
  puts "  Ruby Dir.glob: #{ruby_ms}ms"
  puts
end
