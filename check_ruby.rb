puts Dir.glob("file.{txt,c}").inspect
puts Dir.glob("file.{txt,c}", sort: false).inspect
