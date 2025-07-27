#ifndef OVIS_AST_NODES_XINT_FEATURE_HPP
#define OVIS_AST_NODES_XINT_FEATURE_HPP

namespace ovis::ast
{

    namespace implementation
    {

        template <typename = void>
        class xint_feature
        {
        public:
        protected:
            bool m_is_signed;

            explicit xint_feature(bool p_is_signed)
                : m_is_signed(p_is_signed) {}

        private:
        public:
        };

    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_XINT_FEATURE_HPP