#!/usr/bin/env ruby
# Test if Ruby normalizes continuous ** patterns

require 'fileutils'

# Create test directory structure
Dir.chdir('/tmp') do
  FileUtils.rm_rf('test_recursive_norm')
  FileUtils.mkdir_p('test_recursive_norm')
  Dir.chdir('test_recursive_norm') do
    # Create structure:
    # ./file.txt
    # ./dir1/file.txt
    # ./dir1/dir2/file.txt
    # ./.hidden/file.txt
    # ./.hidden/.subhidden/file.txt

    File.write('file.txt', 'root')
    FileUtils.mkdir_p('dir1/dir2')
    File.write('dir1/file.txt', 'dir1')
    File.write('dir1/dir2/file.txt', 'dir2')

    FileUtils.mkdir_p('.hidden/.subhidden')
    File.write('.hidden/file.txt', 'hidden')
    File.write('.hidden/.subhidden/file.txt', 'subhidden')
  end
end

Dir.chdir('/tmp/test_recursive_norm') do
  tests = [
    # Basic continuous ** patterns
    ['**/file.txt', 'Single ** pattern'],
    ['**/**/file.txt', 'Double ** pattern'],
    ['**/**/**/file.txt', 'Triple ** pattern'],

    # With dot patterns
    ['.**/file.txt', 'Single .** pattern'],
    ['.**/**/file.txt', '.** followed by **'],
    ['.**/.**/file.txt', 'Double .** pattern'],

    # Directory-only patterns
    ['**/', 'Single **/ (dirs only)'],
    ['**/**/', 'Double **/ (dirs only)'],
    ['.**//', 'Single .**/ (dirs only)'],
    ['.**/**/', '.** followed by **/ (dirs only)'],

    # Mixed patterns
    ['**/dir1/**/file.txt', '** with intermediate literal'],
    ['**/dir1/**/**/file.txt', '** with literal and double **'],
  ]

  puts "=" * 80
  puts "Testing Ruby's handling of continuous ** patterns"
  puts "=" * 80
  puts

  tests.each do |pattern, description|
    puts "Pattern: #{pattern.inspect}"
    puts "Description: #{description}"

    results = Dir.glob(pattern, File::FNM_DOTMATCH).sort

    puts "Results (#{results.size}):"
    if results.empty?
      puts "  (no matches)"
    else
      results.each do |r|
        puts "  #{r}"
      end
    end

    # Check if results are duplicated
    if results.size != results.uniq.size
      puts "  ⚠️  WARNING: Duplicates found!"
      duplicates = results.group_by { |x| x }.select { |k, v| v.size > 1 }
      duplicates.each do |path, occurrences|
        puts "    #{path} appears #{occurrences.size} times"
      end
    end

    puts
  end

  # Special test: Compare single vs double **
  puts "=" * 80
  puts "Comparison: Does ** == **/**?"
  puts "=" * 80
  puts

  single = Dir.glob('**/file.txt').sort
  double = Dir.glob('**/**/file.txt').sort
  triple = Dir.glob('**/**/**/file.txt').sort

  puts "**/file.txt: #{single.inspect}"
  puts "**/**/file.txt: #{double.inspect}"
  puts "**/**/**/file.txt: #{triple.inspect}"
  puts

  if single == double && double == triple
    puts "✅ All three patterns produce IDENTICAL results"
    puts "   → Ruby normalizes continuous ** patterns"
  else
    puts "❌ Patterns produce DIFFERENT results"
    puts "   → Ruby does NOT normalize continuous ** patterns"

    if single != double
      puts "\nDifference between ** and **/**:"
      puts "  Only in **/: #{(single - double).inspect}"
      puts "  Only in **/**/: #{(double - single).inspect}"
    end

    if double != triple
      puts "\nDifference between **/** and **/**/**:"
      puts "  Only in **/**/: #{(double - triple).inspect}"
      puts "  Only in **/**/**/: #{(triple - double).inspect}"
    end
  end

  # Test with .**/ patterns
  puts
  puts "=" * 80
  puts "Comparison: Does .**/ == .**/.**/"
  puts "=" * 80
  puts

  dot_single = Dir.glob('.**/file.txt', File::FNM_DOTMATCH).sort
  dot_double = Dir.glob('.**/**/file.txt', File::FNM_DOTMATCH).sort

  puts ".**/file.txt: #{dot_single.inspect}"
  puts ".**/**/file.txt: #{dot_double.inspect}"
  puts

  if dot_single == dot_double
    puts "✅ Both patterns produce IDENTICAL results"
    puts "   → Ruby normalizes .** + ** patterns"
  else
    puts "❌ Patterns produce DIFFERENT results"
    puts "   → Ruby does NOT normalize .** + ** patterns"
    puts "\nDifference:"
    puts "  Only in .**/: #{(dot_single - dot_double).inspect}"
    puts "  Only in .**/**/: #{(dot_double - dot_single).inspect}"
  end
end

puts
puts "=" * 80
puts "Cleanup"
puts "=" * 80
FileUtils.rm_rf('/tmp/test_recursive_norm')
puts "Test directory removed"
