#!/usr/bin/ruby

# A wrapper over the awesome (but abandoned) Ronn that:
# - Uses a *correct* basename function.
# - Writes outputs to the specified output directory.

require 'ronn'

class Document < Ronn::Document
  def basename(type=nil)
    r = File.basename(@basename, '.ronn')
    r += ".#{type}" if type != 'roff'
    r
  end
end

outdir = ARGV.shift

docs = ARGV.map{|arg| Document.new arg }
docs.each do |doc|
  roff = doc.convert 'roff'
  html = doc.convert 'html'

  ['roff', 'html'].each do |format|
    File.open("#{outdir}/#{File.basename doc.path_for format}", 'w') do |fp|
      fp.write doc.convert format
    end
  end
end
