#!/usr/bin/env ruby
# P0最適化のベンチマーク

require 'benchmark'
require 'ffi'

module RbcGlob
  extend FFI::Library
  ffi_lib '/workspaces/dirglob/build/src/librbcglob.a'

  attach_function :rbcglob_glob, [:string, :int, :pointer, :pointer], :int
  attach_function :rbcglob_free_result, [:pointer], :void
end

def rbcglob_ffi(pattern, flags = 0)
  result_ptr = FFI::MemoryPointer.new(:pointer)
  count_ptr = FFI::MemoryPointer.new(:size_t)

  ret = RbcGlob.rbcglob_glob(pattern, flags, result_ptr, count_ptr)
  return [] if ret != 0

  result = result_ptr.read_pointer
  count = count_ptr.read_size_t

  paths = []
  count.times do |i|
    str_ptr = result.get_pointer(i * FFI::Pointer.size)
    paths << str_ptr.read_string unless str_ptr.null?
  end

  RbcGlob.rbcglob_free_result(result)
  paths
end

puts "=== P0 Optimization Benchmark ==="
puts

# パターンごとにテスト
test_cases = [
  { pattern: "*.md", description: "Literal suffix (*.md)" },
  { pattern: "src/*.c", description: "Literal prefix + suffix (src/*.c)" },
  { pattern: "src/**/*.c", description: "Recursive with suffix (src/**/*.c)" },
  { pattern: "tests/**/*", description: "Recursive all (tests/**/*)" },
]

iterations = 100

test_cases.each do |tc|
  pattern = tc[:pattern]
  description = tc[:description]

  puts "Pattern: #{pattern} - #{description}"

  # Ruby Dir.glob
  ruby_result = Dir.glob(pattern, File::FNM_DOTMATCH)

  # rbcglob (FFI)
  rbcglob_result = rbcglob_ffi(pattern, 0x04) # FNM_DOTMATCH = 0x04

  # 結果の数を確認
  puts "  Ruby: #{ruby_result.size}, rbcglob: #{rbcglob_result.size}"

  # ベンチマーク
  ruby_time = Benchmark.measure do
    iterations.times { Dir.glob(pattern, File::FNM_DOTMATCH) }
  end

  rbcglob_time = Benchmark.measure do
    iterations.times { rbcglob_ffi(pattern, 0x04) }
  end

  ruby_ms = (ruby_time.real * 1000 / iterations).round(2)
  rbcglob_ms = (rbcglob_time.real * 1000 / iterations).round(2)

  ratio = (rbcglob_ms / ruby_ms).round(2)
  symbol = ratio < 1.0 ? "⚡" : "⚠️"

  puts "  Ruby: #{ruby_ms}ms, rbcglob: #{rbcglob_ms}ms → #{ratio}x #{symbol}"
  puts
end
