
Dir.chdir("../tests/fixtures") do
  res = Dir.glob("**/?").sort
  puts "Count: #{res.count}"
  res.each { |x| puts x }
end
puts "---"
puts "FNM_DOTMATCH check:"
puts File.fnmatch("?", ".")
