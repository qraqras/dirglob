require 'benchmark'

def bench(name, pattern, string, iterations)
  puts "--- #{name} ---"
  puts "Pattern: #{pattern}"
  puts "String length: #{string.length}"

  start_time = Time.now
  iterations.times do
    File.fnmatch(pattern, string)
  end
  end_time = Time.now

  duration_ms = (end_time - start_time) * 1000.0
  puts "Ruby File.fnmatch: #{sprintf('%.3f', duration_ms)} ms total (#{iterations} iterations)"
end

evil_pattern = "*a*b*c*d*e*f*g*h*i*j*"
long_string = "a" * 49
bench("Complex Wildcards", evil_pattern, long_string, 500000)
