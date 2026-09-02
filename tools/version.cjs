// The project version, for anything that needs it outside CMake.
const fs = require ('fs');
const path = require ('path');
const txt = fs.readFileSync (path.join (__dirname, '..', 'CMakeLists.txt'), 'utf8');
const m = txt.match (/project\s*\([^)]*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)/i);
module.exports = m ? m[1] : '0.0.0';
