
sigfm_sources = ['sigfm.cpp']

# needed packages to have:
# opencv-core-dev
# opencv-features2d-dev

opencv_inc = include_directories('/usr/include/opencv4', is_system: true)
opencv = cc.find_library('opencv_core')
opencv_features2d = cc.find_library('opencv_features2d')

doctest = dependency('doctest', required: true, version: ['>=2.0.0', '<3.0.0'])

libsigfm = static_library(
        'sigfm',
        sigfm_sources,
        include_directories: opencv_inc,
        dependencies: [opencv, opencv_features2d],
)
sigfm_tests = executable(
        'sigfm-tests', 
        ['./tests.cpp'], 
        include_directories: opencv_inc,
        dependencies: [doctest, opencv, opencv_features2d], 
        link_with: [libsigfm]
)
