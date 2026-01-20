#!/usr/bin/env ruby
require 'benchmark'
require 'timeout'

# rbc_globのRuby拡張をロード
require_relative 'ext/rbcglob/rbcglob'

# ワークディレクトリを変更
Dir.chdir(__dir__)

# ベンチマーク設定
PATTERNS = [
  { name: '単純パターン (ルート)', pattern: '*.c', iter: 1000 },
  { name: '再帰パターン (全体)', pattern: '**/*.c', iter: 100 },
  { name: '再帰パターン (src)', pattern: 'src/**/*.c', iter: 500 },
  { name: '再帰パターン (tests)', pattern: 'tests/**/*.c', iter: 500 },
  { name: 'ブレース展開 (src)', pattern: 'src/**/*.{c,h}', iter: 200 },
  { name: '複数パス', pattern: '{src,tests,examples}/**/*.c', iter: 100 },
]

puts "=" * 80
puts "  Ruby Dir.glob vs RBCGlob.glob (Ruby C拡張)"
puts "=" * 80
puts
puts "プロジェクト: #{Dir.pwd}"
puts "Ruby: #{RUBY_VERSION}"
puts

results = []

PATTERNS.each do |config|
  name = config[:name]
  pattern = config[:pattern]
  iter = config[:iter]

  puts "\n【#{name}】"
  puts "パターン: #{pattern}"
  puts "-" * 80

  # Dir.glob
  dir_time = nil
  dir_matches = nil
  begin
    Timeout.timeout(15) do
      dir_matches = Dir.glob(pattern).sort
      dir_time = Benchmark.measure {
        iter.times { Dir.glob(pattern) }
      }.real * 1000 / iter
    end
  rescue Timeout::Error
    dir_time = nil
    puts "Dir.glob: タイムアウト（15秒超過）"
  end

  # RBCGlob.glob
  rbc_time = nil
  rbc_matches = nil
  begin
    Timeout.timeout(15) do
      rbc_matches = RBCGlob.glob(pattern).sort
      rbc_time = Benchmark.measure {
        iter.times { RBCGlob.glob(pattern) }
      }.real * 1000 / iter
    end
  rescue Timeout::Error
    rbc_time = nil
    puts "RBCGlob.glob: タイムアウト（15秒超過）"
  end

  # 結果出力
  if dir_time
    puts "Dir.glob:     #{sprintf('%8.3f', dir_time)} ms/iter (#{dir_matches.size} matches)"
  end
  if rbc_time
    puts "RBCGlob.glob: #{sprintf('%8.3f', rbc_time)} ms/iter (#{rbc_matches.size} matches)"
  end

  # マッチ差分確認
  if dir_matches && rbc_matches
    dir_only = dir_matches - rbc_matches
    rbc_only = rbc_matches - dir_matches
    if dir_only.any? || rbc_only.any?
      puts "警告: マッチ結果が異なります"
      if dir_only.any?
        puts "  Dir.globのみ: #{dir_only.take(5).join(', ')}#{dir_only.size > 5 ? '...' : ''} (#{dir_only.size}個)"
      end
      if rbc_only.any?
        puts "  RBCGlobのみ: #{rbc_only.take(5).join(', ')}#{rbc_only.size > 5 ? '...' : ''} (#{rbc_only.size}個)"
      end
    end
  end

  if dir_time && rbc_time && dir_matches.size == rbc_matches.size
    speedup = dir_time / rbc_time
    symbol = speedup > 10 ? '⭐⭐' : (speedup > 5 ? '⭐' : '')
    puts "高速化:       #{sprintf('%8.2f', speedup)}x #{symbol}"
    results << {
      name: name,
      pattern: pattern,
      dir_time: dir_time,
      rbc_time: rbc_time,
      speedup: speedup,
      matches: dir_matches.size
    }
  end
end

if results.any?
  puts "\n"
  puts "=" * 80
  puts "  サマリー (マッチ数一致のみ)"
  puts "=" * 80
  puts
  puts sprintf("%-25s  %12s  %12s  %10s  %s",
               "パターン", "Dir.glob(ms)", "RBCGlob(ms)", "高速化", "マッチ数")
  puts "-" * 80

  results.each do |r|
    puts sprintf("%-25s  %12.3f  %12.3f  %9.2fx  %d",
                 r[:name],
                 r[:dir_time],
                 r[:rbc_time],
                 r[:speedup],
                 r[:matches])
  end

  puts
  avg_speedup = results.map { |r| r[:speedup] }.sum / results.size
  max_speedup = results.map { |r| r[:speedup] }.max
  puts "平均高速化: #{sprintf('%.2f', avg_speedup)}x"
  puts "最大高速化: #{sprintf('%.2f', max_speedup)}x"
end
