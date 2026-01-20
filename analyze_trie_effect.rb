#!/usr/bin/env ruby
require 'benchmark'
require 'timeout'

require_relative 'ext/rbcglob/rbcglob'

Dir.chdir(__dir__)

# ブレース展開パターンの比較
PATTERNS = [
  { name: 'ブレース展開 (2パターン)', pattern: 'src/**/*.{c,h}', iter: 1000 },
  { name: '展開後 個別実行', patterns: ['src/**/*.c', 'src/**/*.h'], iter: 1000 },
  { name: 'ブレース展開 (10パターン)', pattern: 'src/**/*.{c,h,txt,md,json,yml,xml,sh,py,rb}', iter: 500 },
  { name: '複雑なブレース', pattern: '{src,tests,examples}/**/*.{c,h}', iter: 500 },
]

puts "=" * 80
puts "  トライ木の効果検証: ブレース展開パターン"
puts "=" * 80
puts

PATTERNS.each do |config|
  name = config[:name]
  pattern = config[:pattern]
  patterns = config[:patterns]
  iter = config[:iter]

  puts "\n【#{name}】"

  if pattern
    puts "パターン: #{pattern}"

    # Dir.glob
    dir_matches = Dir.glob(pattern).sort
    dir_time = Benchmark.measure {
      iter.times { Dir.glob(pattern) }
    }.real * 1000 / iter

    # RBCGlob.glob
    rbc_matches = RBCGlob.glob(pattern).sort
    rbc_time = Benchmark.measure {
      iter.times { RBCGlob.glob(pattern) }
    }.real * 1000 / iter

    puts "Dir.glob:     #{sprintf('%8.3f', dir_time)} ms/iter (#{dir_matches.size} matches)"
    puts "RBCGlob.glob: #{sprintf('%8.3f', rbc_time)} ms/iter (#{rbc_matches.size} matches)"

    if dir_matches.size == rbc_matches.size
      speedup = dir_time / rbc_time
      puts "高速化:       #{sprintf('%8.2f', speedup)}x"
    else
      puts "警告: マッチ数が異なります (Dir:#{dir_matches.size} vs RBC:#{rbc_matches.size})"
    end
  elsif patterns
    puts "パターン: #{patterns.inspect}"

    # Dir.glob - 個別実行
    dir_matches = patterns.flat_map { |p| Dir.glob(p) }.uniq.sort
    dir_time = Benchmark.measure {
      iter.times { patterns.flat_map { |p| Dir.glob(p) }.uniq }
    }.real * 1000 / iter

    # RBCGlob.glob - 個別実行してマージ
    rbc_matches = patterns.flat_map { |p| RBCGlob.glob(p) }.uniq.sort
    rbc_time = Benchmark.measure {
      iter.times { patterns.flat_map { |p| RBCGlob.glob(p) }.uniq }
    }.real * 1000 / iter

    puts "Dir.glob (個別):     #{sprintf('%8.3f', dir_time)} ms/iter (#{dir_matches.size} matches)"
    puts "RBCGlob.glob (個別): #{sprintf('%8.3f', rbc_time)} ms/iter (#{rbc_matches.size} matches)"

    if dir_matches.size == rbc_matches.size
      speedup = dir_time / rbc_time
      puts "高速化:             #{sprintf('%8.2f', speedup)}x"
    end
  end
end

puts "\n"
puts "=" * 80
puts "  分析"
puts "=" * 80
puts <<~ANALYSIS

トライ木の効果が薄い理由：

1. **小規模ディレクトリ構造**
   - `src`は単一の小さなディレクトリツリー
   - スキャン対象が限定的（5ファイル程度）
   - トライ木のオーバーヘッドと同等の時間

2. **ブレース展開の処理**
   - `{c,h}`は内部で2回のスキャンに展開
   - トライ木でもディレクトリスキャンは共有されるが、
     ファイル数が少ないため効果が見えにくい

3. **トライ木が効果的なケース**
   - 大規模なディレクトリツリー
   - 多数のパターン（10個以上）
   - 共通のディレクトリプレフィックスを持つパターン

ANALYSIS
