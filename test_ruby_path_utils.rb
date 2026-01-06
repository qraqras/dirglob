#!/usr/bin/env ruby
# Test script to verify Ruby 4.0 path utility compatibility

puts "Testing Ruby File path utilities"
puts "=" * 50

# Test dirname
puts "\n=== File.dirname ==="
test_paths = [
  "/home/gumby/work/ruby.rb",
  "/home/gumby/",
  "/",
  "ruby.rb",
  ""
]

test_paths.each do |path|
  puts "File.dirname(\"#{path}\") => \"#{File.dirname(path)}\""
end

puts "\n=== File.dirname with level ==="
puts "File.dirname(\"/home/gumby/work/ruby.rb\", 2) => \"#{File.dirname("/home/gumby/work/ruby.rb", 2)}\""
puts "File.dirname(\"/home/gumby/work/ruby.rb\", 4) => \"#{File.dirname("/home/gumby/work/ruby.rb", 4)}\""

# Test basename
puts "\n=== File.basename ==="
test_paths = [
  ["/home/gumby/work/ruby.rb", nil],
  ["/home/gumby/work/ruby.rb", ".rb"],
  ["/home/gumby/work/ruby.rb", ".*"],
  ["/home/gumby/", nil],
  ["/", nil],
  ["ruby.rb", nil]
]

test_paths.each do |path, suffix|
  if suffix
    puts "File.basename(\"#{path}\", \"#{suffix}\") => \"#{File.basename(path, suffix)}\""
  else
    puts "File.basename(\"#{path}\") => \"#{File.basename(path)}\""
  end
end

# Test extname
puts "\n=== File.extname ==="
test_paths = [
  "test.rb",
  "a/b/d/test.rb",
  ".a/b/d/test.rb",
  ".profile",
  ".profile.sh",
  "test",
  "foo.",
  "test.tar.gz"
]

test_paths.each do |path|
  puts "File.extname(\"#{path}\") => \"#{File.extname(path)}\""
end

puts "\n" + "=" * 50
puts "All tests completed"
