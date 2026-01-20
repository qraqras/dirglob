#!/usr/bin/env ruby
require 'benchmark'
require 'timeout'

# rbc_globのRuby拡張をロード
require_relative 'ext/rbcglob/rbcglob'

# ワークディレクトリを変更
Dir.chdir(__dir__)

# ベンチマーク設定
PATTERNS = {
  '単純パターン' => '*.c',
  '再帰パターン' => '**/*.c',
  '複数展開' => '{*.c,*.h}',
  '深いパス' => 'src/core/**/*.c',
  'ブレース複雑' => '{src,tests}/**/*.{c,h}'
}

ITERATIONS = {
  '単純パターン' => 1000,
  '再帰パターン' => 100,
  '複数展開' => 1000,
  '深いパス' => 500,
  'ブレース複雑' => 100
}

puts "=" * 70
puts "  Ruby Dir.glob vs RBCGlob.glob (Ruby拡張)"
puts "=" * 70
puts
puts "プロジェクト: #{Dir.pwd}"
puts "Ruby: #{RUBY_VERSION}"
puts

results = []

PATTERNS.each do |name, pattern|
  iter = ITERATIONS[name]

  puts "\n【#{name}】パターン: #{pattern}"
  puts "-" * 70

  # Dir.glob
  dir_time = nil
  dir_matches = nil
  begin
    Timeout.timeout(10) do
      dir_matches = Dir.glob(pattern)
      dir_time = Benchmark.measure {
        iter.times { Dir.glob(pattern) }
      }.real * 1000 / iter
    end
  rescue Timeout::Error
    dir_time = nil
    puts "Dir.glob: タイムアウト（10秒超過）"
  end

  # RBCGlob.glob
  rbc_time = nil
  rbc_matches = nil
  begin
    Timeout.timeout(10) do
      rbc_matches = RBCGlob.glob(pattern)
      rbc_time = Benchmark.measure {
        iter.times { RBCGlob.glob(pattern) }
      }.real * 1000 / iter
    end
  rescue Timeout::Error
    rbc_time = nil
    puts "RBCGlob.glob: タイムアウト（10秒超過）"
  end

  # 結果出力
  if dir_time
    puts "Dir.glob:    #{sprintf('%.3f', dir_time)} ms/iter (#{dir_matches.size} matches)"
  end
  if rbc_time
    puts "RBCGlob.glob: #{sprintf('%.3f', rbc_time)} ms/iter (#{rbc_matches.size} matches)"
  end

  if dir_time && rbc_time
    speedup = dir_time / rbc_time
    puts "高速化:      #{sprintf('%.2f', speedup)}x #{speedup > 5 ? '⭐' : ''}"
    results << {
      name: name,
      pattern: pattern,
      dir_time: dir_time,
      rbc_time: rbc_time,
      speedup: speedup,
      dir_matches: dir_matches.size,
      rbc_matches: rbc_matches.size
    }
  end
end

puts "\n"
puts "=" * 70
puts "  サマリー"
puts "=" * 70
puts
puts sprintf("%-15s  %12s  %12s  %8s  %s",
             "パターン", "Dir.glob(ms)", "RBCGlob(ms)", "高速化", "マッチ数")
puts "-" * 70

results.each do |r|
  puts sprintf("%-15s  %12.3f  %12.3f  %7.2fx  %d/%d",
               r[:name],
               r[:dir_time],
               r[:rbc_time],
               r[:speedup],
               r[:dir_matches],
               r[:rbc_matches])
end

puts
avg_speedup = results.map { |r| r[:speedup] }.sum / results.size
puts "平均高速化: #{sprintf('%.2f', avg_speedup)}x"
