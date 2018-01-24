#!/usr/bin/ruby

# A wrapper over the awesome (but abandoned) Ronn that:
# - Uses a *correct* basename function.
# - Writes outputs to the specified output directory.

script_dir = File.dirname(__FILE__)

$LOAD_PATH.insert 0, "#{script_dir}/ronn/lib"

ENV['RONN_STYLE'] = "#{script_dir}/../web"

require 'ronn'

class Document < Ronn::Document
  def basename(type=nil)
    r = File.basename(@basename, '.ronn')
    r += ".#{type}" if type != 'roff'
    r
  end

  def to_html
    super
      .gsub('<head>', "<head>\n  <meta name='viewport' content='width=device-width'>")
      .gsub("<li class='tc'></li>", '')
  end

  def name
    basename.gsub /\.\d\.$/, ''
  end

  def to_html_fragment(wrap_class=nil)
    names = data[1..data =~ /\n/].split('--', 2)[0].strip.gsub("(#{section})", '')

    super.gsub "#{name}</code> - ", "#{names}</code> - " \
  end
end

dstman = ARGV.shift
dsthtml = ARGV.shift

dstdirs = {html: dsthtml, roff: dstman}

docs = ARGV.map{|arg| Document.new arg, styles: ['man-extra']}
docs.each do |doc|
  [:html, :roff].each do |format|
    dst = "#{dstdirs[format]}/#{File.basename doc.path_for format.to_s}"
    File.open(dst, 'w') do |fp|
      fp.write doc.convert format.to_s
    end
  end
end
