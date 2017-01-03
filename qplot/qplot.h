#pragma once

#include <ostream>
#include <sstream>
#include <tuple>
#include <type_traits>

// Enable this macro definition to send all output to stdout in exactly the form as if sent to the subprocess
// #define QPLOT_DEBUG

// Enable this macro definition to use boost/variant instead of C++17 std::variant
// #define WITH_BOOST_VARIANT


#ifdef WITH_BOOST_VARIANT
    #include <boost/variant.hpp>
#else
    #include <variant>
#endif


// Name qp::variant as either boost or stl
namespace qp {

#ifdef WITH_BOOST_VARIANT
    using boost::variant;
    using monostate = boost::blank;

    template <typename... Args>
    auto visit(Args&&... args) {
      return boost::apply_visitor(std::forward<Args>(args)...);
    }
#else
    using std::variant;
    using monostate = std::monostate;

    template <typename... Args>
    auto visit(Args&&... args) {
      return std::visit(std::forward<Args>(args)...);
    }
#endif

}

#include "subprocess.h"
#include "util.h"

namespace qp {

// With specializations in client code
template <typename T>
struct has_styles;


// Map a tuple of `types...` to a tuple of `has_styles<types...>::type`
// There's probably a cleaner way..
struct foo { using type = void; };

template <typename... Ts>
struct get_styles;

template <typename... Xs>
struct get_styles<std::tuple<Xs...>, std::tuple<>> : foo { using type = std::tuple<Xs...>; };

template <typename U, typename... Xs, typename... Ts>
struct get_styles<std::tuple<Xs...>, std::tuple<U, Ts...>> : get_styles<std::tuple<Xs..., typename has_styles<U>::type>, std::tuple<Ts...>> {};

template <typename Tuple>
using styles_from_types = typename get_styles<std::tuple<>, Tuple>::type;



// This adds an implementation detail necessary for all uses
// For more information see the Subprocess section of README.md and examples_process.cpp
struct PythonHeader
{
    void operator()(Subprocess<Python>& python) const
    {
        python.out()
            << "# This header has been added by Qplot. See qplot.h\n"
            << "import os\n"
            << "qp_data_in = list()\n"
            << "\n";

        for (int i=0; i<python.numChannels(); i++) {
            python.out()
                << "os.close(" << python.fd_w(i) << ")\n"
                << "qp_data_in.append(os.fdopen(" << python.fd_r(i) << ", 'r'))\n"
                << "\n";
        }
    }
};

template <typename P, typename S>
void addPrivateHeader(Qplot<P,S>& python)
{
}

template <typename S>
void addPrivateHeader(Qplot<Python,S>& python)
{
    python.addToHeader(PythonHeader{});
}

template <typename S>
void addPrivateHeader(Qplot<Gnuplot,S>& python)
{
}

template <typename P, typename X>
class Qplot
{
    std::unique_ptr<Subprocess<P>> subprocess_;
    std::ostringstream header_;

    using S = styles_from_types<X>;

    S styles_;
    S stylesHeader_;

public:
    using process = P;
    using styles = S;

    template <typename... Ts>
    Qplot(Ts&&... args) :
        subprocess_(std::make_unique<Subprocess<P>>())
    {
        addPrivateHeader(*this);
        addToHeader(args...);
    }

    void processArgs() {}

	// Arg is a canvas-style only
    template <typename T, typename... Ts,
              std::enable_if_t< is_canvas_style<T,P>::value &&
                               !is_object_style<T,P>::value, int> = 0>
    void processArgs(const T& style, const Ts&... args)
    {
        style(*subprocess_);
		processArgs(args...);
    }

    // Arg is an object-style only
    template <typename T, typename... Ts,
              std::enable_if_t< is_object_style<T,P>::value &&
                               !is_canvas_style<T,P>::value, int> = 0>
    void processArgs(const T& style, const Ts&... args)
    {
        // Update styles_ with this style for all variants in style_ that support it
        constexpr size_t NumStyles = std::tuple_size<S>::value;
        TupleUpdater<T, S, NumStyles>::update(styles_, style);

        processArgs(args...);
    }

    // Arg is both an object-style and canvas_style
    template <typename T, typename... Ts,
              std::enable_if_t<is_object_style<T,P>::value &&
                               is_canvas_style<T,P>::value, int> = 0>
    void processArgs(const T& style, const Ts&... args)
    {
        style(*subprocess_);

        constexpr size_t NumStyles = std::tuple_size<S>::value;
        TupleUpdater<T, S, NumStyles>::update(styles_, style);

        processArgs(args...);
    }

    // Arg is an object to plot
    template <typename T, typename... Ts,
              std::enable_if_t<!is_object_style<T,P>::value &&
                               !is_canvas_style<T,P>::value, int> = 0>
    void processArgs(const T& obj, const Ts&... args)
    {
		// Get the style variant that's currently associated with the arg type T
        constexpr size_t key = tuple_contains_type<typename has_styles<T>::type, S>::value;
        auto styleVar = std::get<key>(styles_);

		// Plot this object
        qp::visit([this,&obj](auto&& style) {
            style(*subprocess_, obj);
		}, styleVar);

		processArgs(args...);
    }

    template <typename... Ts>
	void plot(const Ts&... args)
	{
        // Replay the header
        subprocess_->out() << header_.str();

        // Reload state for the chosen alternatives
        styles_ = stylesHeader_;

        // Recurse into arguments
		processArgs(args...);

        // Finally, close this process and reopen with a fresh instance
        // + This ends out code stream to the process, which e.g. for python alows the process to start executin
        // + Also important that each call to plot (aside from the intentional header) is stateless
        subprocess_.reset();  // destory first
        subprocess_ = std::make_unique<Subprocess<P>>();
	}

    template <typename... Ts>
    void addToHeader(const Ts&... args)
    {
        // Everything to cfout goes to our local ostringstream
        auto cout_buffer = subprocess_->out().rdbuf();
        subprocess_->out().rdbuf(header_.rdbuf());

        // Also save the state of the chosen alternatives
        stylesHeader_ = styles_;

        // Capture in local header_ buffer
        processArgs(args...);

        // Swap back
        subprocess_->out().rdbuf(cout_buffer);
    }

    // cf_ostream& out()      { return subprocess_->out(); }
    // fd_ostream& data_out() { return subprocess_->data_out(); }

    // int fd_r() { return subprocess_->fd_r(); }
    // int fd_w() { return subprocess_->fd_w(); }
};

} // namespace qp


// Main convenience method for one-liner plotting
// template <typename... Ts>
// void qplot(const Ts&... args)
// {
//     static Qplot qp;
//     qp.plot(args...);
// }


/*
// These non-members provide another level of indirection to enable completely overloading how an object+style is plotted
// Currently disabled as unused, but left here as a future possibility

template <typename P>
class Qplot;

template <typename P, typename Style, typename T,
          std::enable_if_t<!is_object_style<Style,P>::value, int> = 0>
void plotObject(Qplot<P>& process, const Style& style, const T& obj)
{
    // Do nothing where no plot member exists for this Subprocess<P>
}

// Note that this will be instantiated for all style variants associated with the obj type (regardless of the actual obj type)
template <typename P, typename Style, typename T,
          std::enable_if_t<is_object_style<Style,P>::value, int> = 0>
void plotObject(Qplot<P>& process, const Style& style, const T& obj)
{
    style(process, obj);
}


template <typename P, typename Style,
          std::enable_if_t<!is_canvas_style<Style,P>::value, int> = 0>
void plotStyle(Qplot<P>& process, const Style& style)
{
    // Do nothing where no plot member exists for this Subprocess<P>
}

template <typename P, typename Style,
          std::enable_if_t<is_canvas_style<Style,P>::value, int> = 0>
void plotStyle(Qplot<P>& process, const Style& style)
{
    style(process);
}
*/
